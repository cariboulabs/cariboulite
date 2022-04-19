#include "Cariboulite.hpp"
#include "cariboulite_config/cariboulite_config_default.h"

//=================================================================
static void caribou_stream_data_event( void *ctx, 
                                        void *service_context,
                                        caribou_smi_stream_type_en type,
                                        caribou_smi_channel_en ch,
                                        size_t sample_count,
                                        caribou_smi_sample_complex_int16 *cmplx_vec,
										caribou_smi_sample_meta *meta_vec,
                                        size_t buffers_capacity_samples)
{
    cariboulite_st* sys = (cariboulite_st*)ctx;
    Cariboulite *obj = (Cariboulite*)service_context;

    int dir = (type == caribou_smi_stream_type_read) ? SOAPY_SDR_RX : SOAPY_SDR_TX;
    int channel = (ch == caribou_smi_channel_900) ? cariboulite_channel_s1g : cariboulite_channel_6g;

    switch(type)
    {
        //-------------------------------------------------------
        case caribou_smi_stream_type_read:
            {
                int sample_queue_index = CARIBOU_SMI_GET_STREAM_ID(type, ch);
                obj->sample_queues[sample_queue_index]->Write(cmplx_vec, sample_count, 0, 10000L);
            }
            break;

        //-------------------------------------------------------
        case caribou_smi_stream_type_write:
            {
                int sample_queue_index = CARIBOU_SMI_GET_STREAM_ID(dir, channel);
                printf("Wrote to sample_queue_index %d\n", sample_queue_index);
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
    //printf("getStreamFormats\n");
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
    //printf("getNativeStreamFormat\n");
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
    //printf("getStreamArgsInfo start\n");
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
    int cw = (args.count("CW") != 0) ? std::atoi(args.at("CW").c_str()) : 0;
    SoapySDR_logf(SOAPY_SDR_INFO, "setupStream: dir= %s, format= %s, is_cw= %d, ch= %d", 
                                direction == SOAPY_SDR_TX?"TX":"RX", format.c_str(), cw, 
                                channels.size()?channels[0]:0);
    
    std::vector<size_t> channels_internal = channels;

    // default channel - sub1GHz
    if ( channels_internal.size() == 0 )
    {
        channels_internal.push_back(cariboulite_channel_s1g);
    }

    // currently we take only the first channel
    size_t ch = channels_internal[0];

    // calculate the stream_id
    caribou_smi_stream_type_en type = (direction == SOAPY_SDR_RX) ? caribou_smi_stream_type_read : caribou_smi_stream_type_write;
    caribou_smi_channel_en channel = (ch == cariboulite_channel_s1g) ? caribou_smi_channel_900 : caribou_smi_channel_2400;
    int stream_id = CARIBOU_SMI_GET_STREAM_ID(type, channel);
    sample_queues[stream_id]->is_cw  = cw;

    // Setup the SampleQueue's format
    if (format == SOAPY_SDR_CS16)
    {
        sample_queues[stream_id]->chosen_format = CARIBOULITE_FORMAT_INT16;
    }
    else if (format == SOAPY_SDR_CS8)
    {
        sample_queues[stream_id]->chosen_format = CARIBOULITE_FORMAT_INT8;
    }
    else if (format == SOAPY_SDR_CF32)
    {
        sample_queues[stream_id]->chosen_format = CARIBOULITE_FORMAT_FLOAT32;
    }
    else if (format == SOAPY_SDR_CF64)
    {
        sample_queues[stream_id]->chosen_format = CARIBOULITE_FORMAT_FLOAT64;
    }
    else
    {
        SoapySDR_logf(SOAPY_SDR_ERROR, "the specified format %s is not supported", format.c_str());
        throw std::runtime_error( "setupStream invalid format " + format );
    }

    // create the stream (only for non CW streams)
    if (sample_queues[stream_id]->is_cw)
    {
        cariboulite_set_cw_outputs(&radios, (cariboulite_channel_en)ch, false, true);
    }
    else
    {
        cariboulite_set_cw_outputs(&radios, (cariboulite_channel_en)ch, false, false);
        stream_id = caribou_smi_setup_stream(&sess.cariboulite_sys.smi,
                                    type, channel, 
                                    caribou_stream_data_event, 
                                    this);
        if (stream_id < 0)
        {
            throw std::runtime_error( "setupStream caribou_smi_setup_stream failed" );
        }
    }
    
    //SoapySDR_logf(SOAPY_SDR_INFO, "finished setup stream, stream_id = %d, CW=%d", stream_id, cw);
    return (SoapySDR::Stream *)((void*)(stream_id));
}

//========================================================
/*!
     * Close an open stream created by setupStream
     * The implementation may change switches or power-down components.
     * \param stream the opaque pointer to a stream handle
     */
void Cariboulite::closeStream(SoapySDR::Stream *stream)
{
    if (stream == NULL) return;
    int stream_id = (intptr_t)stream;
    
    caribou_smi_destroy_stream(&sess.cariboulite_sys.smi, stream_id);
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
    return 1024 * 1024 / 2 / 4;
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
    //printf("activateStream\n");
    //SoapySDR_logf(SOAPY_SDR_INFO, "activateStream");
    int stream_id = (intptr_t)stream;

    cariboulite_activate_channel(&radios, 
                                (cariboulite_channel_en)sample_queues[stream_id]->stream_channel, 
                                true);

    if ((cariboulite_channel_en)sample_queues[stream_id]->is_cw == 0)
    {
        caribou_smi_run_pause_stream (&sess.cariboulite_sys.smi, (intptr_t)stream, 1);
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
    //SoapySDR_logf(SOAPY_SDR_INFO, "deactivateStream");
    int stream_id = (intptr_t)stream;

    if ((cariboulite_channel_en)sample_queues[stream_id]->is_cw == 0)
    {
        caribou_smi_run_pause_stream (&sess.cariboulite_sys.smi, (intptr_t)stream, 0);
        sleep(1);
    }

    cariboulite_activate_channel(&radios, 
                                (cariboulite_channel_en)sample_queues[stream_id]->stream_channel, 
                                false);
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
    //printf("readStream\n");
    int stream_id = (intptr_t)stream;

    if (sample_queues[stream_id]->stream_dir != SOAPY_SDR_RX)
    {
        // wrong sample queue => wrong stream_id
        return SOAPY_SDR_NOT_SUPPORTED;
    }

    uint32_t metadata = 0;
    int res = 0;
    switch(sample_queues[stream_id]->chosen_format)
    {
        case CARIBOULITE_FORMAT_FLOAT32: 
            {
                res = sample_queues[stream_id]->ReadSamples((sample_complex_float*)buffs[0], numElems, timeoutUs);
            }
            break;
	    case CARIBOULITE_FORMAT_INT16: 
            res = sample_queues[stream_id]->ReadSamples((caribou_smi_sample_complex_int16*)buffs[0], numElems, timeoutUs);
            break;
	    case CARIBOULITE_FORMAT_INT8: 
            res = sample_queues[stream_id]->ReadSamples((sample_complex_int8*)buffs[0], numElems, timeoutUs);
            break;
	    case CARIBOULITE_FORMAT_FLOAT64: 
            res = sample_queues[stream_id]->ReadSamples((sample_complex_double*)buffs[0], numElems, timeoutUs);
            break;
        default: return -1; break;
    }

    return res;
}