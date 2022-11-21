#include "Cariboulite.hpp"
#include "cariboulite_config/cariboulite_config_default.h"

//=================================================================
void caribou_stream_data_event( void *ctx, 
								void *service_context,
								caribou_smi_stream_type_en type,
								caribou_smi_channel_en ch,
								size_t sample_count,
								caribou_smi_sample_complex_int16 *cmplx_vec,
								caribou_smi_sample_meta *meta_vec,
								size_t buffers_capacity_samples)
{
    //cariboulite_st* sys = (cariboulite_st*)ctx;
    Cariboulite *soapy_obj = (Cariboulite*)service_context;

	// Basic sanity checking
	cariboulite_channel_en ch_type = ch == caribou_smi_channel_900 ? cariboulite_channel_s1g : cariboulite_channel_6g;
	if (soapy_obj->radio.type != ch_type)
	{
		SoapySDR_logf(SOAPY_SDR_ERROR, "caribou_stream_data_event: reaceived wrong CH <=> service_context pair");
		return;
	}

    switch(type)
    {
        //-------------------------------------------------------
        case caribou_smi_stream_type_read:
            {
				// SMI read samples from Caribou and these samples are written into the Soapy buffer
                soapy_obj->sample_queue_rx->Write(cmplx_vec, sample_count, 0, 10000L);
            }
            break;

        //-------------------------------------------------------
        case caribou_smi_stream_type_write:
            {
				// SMI requests to get (read) samples from Soapy and write them to Caribou
				soapy_obj->sample_queue_tx->Read(cmplx_vec, sample_count, 0, 10000L);
            }
            break;

        //-------------------------------------------------------
        case caribou_smi_stream_start:
            {
                SoapySDR_logf(SOAPY_SDR_DEBUG, "start event: stream channel %d, batch length: %d samples", 
                                    ch, buffers_capacity_samples);
            }
            break;

        //-------------------------------------------------------
        case caribou_smi_stream_end:
            {
                SoapySDR_logf(SOAPY_SDR_DEBUG, "end event: stream channel %d, batch length: %d sample", 
                                    ch, buffers_capacity_samples);
            }
            break;

        //-------------------------------------------------------
        default:
            break;
    }
}

//========================================================
/*!
* Query a list of the available stream formats.
* \param direction the channel direction RX or TX
* \param channel an available channel on the device
* \return a list of allowed format strings. See setupStream() for the format syntax.
*/
std::vector<std::string> Cariboulite::getStreamFormats(const int direction, const size_t channel) const
{
    std::vector<std::string> formats;
    formats.push_back(SOAPY_SDR_CS16);
    formats.push_back(SOAPY_SDR_CS8);
    formats.push_back(SOAPY_SDR_CF32);
    formats.push_back(SOAPY_SDR_CF64);
	return formats;
}

//========================================================
/*!
* Get the hardware's native stream format for this channel.
* This is the format used by the underlying transport layer,
* and the direct buffer access API calls (when available).
* \param direction the channel direction RX or TX
* \param channel an available channel on the device
* \param [out] fullScale the maximum possible value
* \return the native stream buffer format string
*/
std::string Cariboulite::getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const
{
    fullScale = (double)((1<<12)-1);
    return SOAPY_SDR_CS16;
}

//========================================================
/*!
* Query the argument info description for stream args.
* \param direction the channel direction RX or TX
* \param channel an available channel on the device
* \return a list of argument info structures
*/
SoapySDR::ArgInfoList Cariboulite::getStreamArgsInfo(const int direction, const size_t channel) const
{
	SoapySDR::ArgInfoList streamArgs;
	return streamArgs;
}

//========================================================
/*!
* Initialize a stream given a list of channels and stream arguments.
* The implementation may change switches or power-up components.
* All stream API calls should be usable with the new stream object
* after setupStream() is complete, regardless of the activity state.
*
* The API allows any number of simultaneous TX and RX streams, but many dual-channel
* devices are limited to one stream in each direction, using either one or both channels.
* This call will throw an exception if an unsupported combination is requested,
* or if a requested channel in this direction is already in use by another stream.
*
* When multiple channels are added to a stream, they are typically expected to have
* the same sample rate. See setSampleRate().
*
* \param direction the channel direction (`SOAPY_SDR_RX` or `SOAPY_SDR_TX`)
* \param format A string representing the desired buffer format in read/writeStream()
* \parblock
*
* The first character selects the number type:
*   - "C" means complex
*   - "F" means floating point
*   - "S" means signed integer
*   - "U" means unsigned integer
*
* The type character is followed by the number of bits per number (complex is 2x this size per sample)
*
*  Example format strings:
*   - "CF32" -  complex float32 (8 bytes per element)
*   - "CS16" -  complex int16 (4 bytes per element)
*   - "CS12" -  complex int12 (3 bytes per element)
*   - "CS4" -  complex int4 (1 byte per element)
*   - "S32" -  int32 (4 bytes per element)
*   - "U8" -  uint8 (1 byte per element)
*
* \endparblock
* \param channels a list of channels or empty for automatic.
* \param args stream args or empty for defaults.
* \parblock
*
*   Recommended keys to use in the args dictionary:
*    - "WIRE" - format of the samples between device and host
* \endparblock
* \return an opaque pointer to a stream handle.
* \parblock
*
* The returned stream is not required to have internal locking, and may not be used
* concurrently from multiple threads.
* \endparblock
*/
SoapySDR::Stream *Cariboulite::setupStream(const int direction, 
                            const std::string &format, 
                            const std::vector<size_t> &channels, 
                            const SoapySDR::Kwargs &args)
{
	// is it a CW TX channel
	int cw = (args.count("CW") != 0) ? std::atoi(args.at("CW").c_str()) : 0;
	if (direction == SOAPY_SDR_RX && cw)
	{
		SoapySDR_logf(SOAPY_SDR_ERROR, "setupStream: CW channel can only be used with TX channel");
		return NULL;
	}

    SoapySDR_logf(SOAPY_SDR_INFO, "setupStream: dir= %s, format= %s, is_cw= %d", 
                                direction == SOAPY_SDR_TX ? "TX" : "RX", 
								format.c_str(), cw);

	// configure the queue
	SoapySDR::Stream* queue = direction == SOAPY_SDR_RX ? sample_queue_rx : sample_queue_tx;
	queue->smi_stream_id = -1;	// by default, the smi stream is not configured (e.g. with CS / BB-FSK...)
    queue->is_cw = cw;
	if (queue->setFormat(format) != 0)
	{
		SoapySDR_logf(SOAPY_SDR_ERROR, "the specified format %s is not supported", format.c_str());
        throw std::runtime_error( "setupStream invalid format " + format );
	}

    // configure the radio channel to CW if needed
	cariboulite_radio_set_cw_outputs(&radio, false, queue->is_cw);

	// configure the smi stream and attach it to the queue
	if (!cw)
    {
		// find out the SMI channel info
		caribou_smi_stream_type_en type = direction == SOAPY_SDR_RX ? caribou_smi_stream_type_read : caribou_smi_stream_type_write;
		caribou_smi_channel_en channel = radio.type == cariboulite_channel_s1g ? caribou_smi_channel_900 : caribou_smi_channel_2400;
        queue->smi_stream_id = caribou_smi_setup_stream(&sess.cariboulite_sys.smi,
														type, 
														channel, 
														caribou_stream_data_event, 
														this);
        if (queue->smi_stream_id < 0)
        {
            throw std::runtime_error( "setupStream caribou_smi_setup_stream failed" );
        }
    }
    
	// Queue inherits Stream class
    return queue;
}

//========================================================
/*!
     * Close an open stream created by setupStream
     * The implementation may change switches or power-down components.
     * \param stream the opaque pointer to a stream handle
     */
void Cariboulite::closeStream(SoapySDR::Stream *stream)
{
	// if it is a CW stream, disable the output
	if (stream->is_cw)
	{
		cariboulite_radio_set_cw_outputs(&radio, false, false);
	}
    
	// check if this is a valid SMI stream
	if (stream->smi_stream_id != -1)
    {
		caribou_smi_destroy_stream(&sess.cariboulite_sys.smi, stream->smi_stream_id);
		stream->smi_stream_id = -1;
	}	
}

//========================================================
/*!
     * Get the stream's maximum transmission unit (MTU) in number of elements.
     * The MTU specifies the maximum payload transfer in a stream operation.
     * This value can be used as a stream buffer allocation size that can
     * best optimize throughput given the underlying stream implementation.
     *
     * \param stream the opaque pointer to a stream handle
     * \return the MTU in number of stream elements (never zero)
     */
size_t Cariboulite::getStreamMTU(SoapySDR::Stream *stream) const
{
    return stream->getMTUSizeElements();
}

//========================================================
/*!
     * Activate a stream.
     * Call activate to prepare a stream before using read/write().
     * The implementation control switches or stimulate data flow.
     *
     * The timeNs is only valid when the flags have SOAPY_SDR_HAS_TIME.
     * The numElems count can be used to request a finite burst size.
     * The SOAPY_SDR_END_BURST flag can signal end on the finite burst.
     * Not all implementations will support the full range of options.
     * In this case, the implementation returns SOAPY_SDR_NOT_SUPPORTED.
     *
     * \param stream the opaque pointer to a stream handle
     * \param flags optional flag indicators about the stream
     * \param timeNs optional activation time in nanoseconds
     * \param numElems optional element count for burst control
     * \return 0 for success or error code on failure
     */
int Cariboulite::activateStream(SoapySDR::Stream *stream,
                                    const int flags,
                                    const long long timeNs,
                                    const size_t numElems)
{
    cariboulite_radio_activate_channel(&radio, true);

    if (!stream->is_cw)
    {
        caribou_smi_run_pause_stream (&sess.cariboulite_sys.smi, stream->smi_stream_id, 1);
    }
    return 0;
}

//========================================================
/*!
     * Deactivate a stream.
     * Call deactivate when not using using read/write().
     * The implementation control switches or halt data flow.
     *
     * The timeNs is only valid when the flags have SOAPY_SDR_HAS_TIME.
     * Not all implementations will support the full range of options.
     * In this case, the implementation returns SOAPY_SDR_NOT_SUPPORTED.
     *
     * \param stream the opaque pointer to a stream handle
     * \param flags optional flag indicators about the stream
     * \param timeNs optional deactivation time in nanoseconds
     * \return 0 for success or error code on failure
     */
int Cariboulite::deactivateStream(SoapySDR::Stream *stream, const int flags, const long long timeNs)
{
	if (!stream->is_cw)
    {
        caribou_smi_run_pause_stream (&sess.cariboulite_sys.smi, stream->smi_stream_id, 0);
		sleep(1);
    }

    cariboulite_radio_activate_channel(&radio, false);
    return 0;
}

//========================================================
/*!
     * Read elements from a stream for reception.
     * This is a multi-channel call, and buffs should be an array of void *,
     * where each pointer will be filled with data from a different channel.
     *
     * **Client code compatibility:**
     * The readStream() call should be well defined at all times,
     * including prior to activation and after deactivation.
     * When inactive, readStream() should implement the timeout
     * specified by the caller and return SOAPY_SDR_TIMEOUT.
     *
     * \param stream the opaque pointer to a stream handle
     * \param buffs an array of void* buffers num chans in size
     * \param numElems the number of elements in each buffer 
     *                  (number of samples - for us its 4 bytes per sample)
     * \param flags optional flag indicators about the result
     * \param timeNs the buffer's timestamp in nanoseconds
     * \param timeoutUs the timeout in microseconds
     * \return the number of elements read per buffer or error code
     */
int Cariboulite::readStream(
            SoapySDR::Stream *stream,
            void * const *buffs,
            const size_t numElems,
            int &flags,
            long long &timeNs,
            const long timeoutUs)
{
	// Verify that it is an RX stream
    if (stream->getInnerStreamType() != SOAPY_SDR_RX)
    {
        return SOAPY_SDR_NOT_SUPPORTED;
    }

    return stream->ReadSamplesGen((void*)buffs[0], numElems, timeoutUs);
}