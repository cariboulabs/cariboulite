#include <math.h>
#include "Cariboulite.hpp"
#include "cariboulite_config/cariboulite_config_default.h"

SoapyCaribouliteSession Cariboulite::sess;

/*******************************************************************
 * Constructor / Destructor
 ******************************************************************/
Cariboulite::Cariboulite(const SoapySDR::Kwargs &args)
{
	SoapySDR_logf(SOAPY_SDR_INFO, "Initializing DeviceID: %s, Label: %s, ChannelType: %s", 
					args.at("device_id").c_str(), 
					args.at("label").c_str(),
					args.at("channel").c_str());

	// Initialize the stream Sample Queues
	sample_queue_tx = new SoapySDR::Stream();
	sample_queue_rx = new SoapySDR::Stream();

	if (!args.at("channel").compare ("HiF"))
	{	
		//sample_queue_tx->AttachStreamId(0, caribou_smi_stream_type_write, caribou_smi_channel_2400);
		//sample_queue_rx->AttachStreamId(1, caribou_smi_stream_type_read, caribou_smi_channel_2400);
		cariboulite_radio_init(&radio, &sess.cariboulite_sys, cariboulite_channel_6g);
	}
	else if (!args.at("channel").compare ("S1G"))
	{
		//sample_queue_tx->AttachStreamId(0, caribou_smi_stream_type_write, caribou_smi_channel_900);
		//sample_queue_rx->AttachStreamId(1, caribou_smi_stream_type_read, caribou_smi_channel_900);
		cariboulite_radio_init(&radio, &sess.cariboulite_sys, cariboulite_channel_s1g);
	}
	else
	{
		throw std::runtime_error( "Channel type is not specified correctly" );
	}
}

//========================================================
Cariboulite::~Cariboulite()
{
	SoapySDR_logf(SOAPY_SDR_INFO, "Desposing radio type '%d'", radio.type);
    cariboulite_radio_dispose(&radio);
	delete sample_queue_tx;
	delete sample_queue_rx;
}

/*******************************************************************
 * Identification API
 ******************************************************************/
SoapySDR::Kwargs Cariboulite::getHardwareInfo() const
{
    //key/value pairs for any useful information
    //this also gets printed in --probe
    SoapySDR::Kwargs args;

    uint32_t serial_number = 0;
    uint32_t deviceId = 0;
    int count = 0;
    cariboulite_get_serial_number((cariboulite_st*)&sess.cariboulite_sys, &serial_number, &count) ;

    args["device_id"] = std::to_string(deviceId);
    args["serial_number"] = std::to_string(serial_number);
    args["hardware_revision"] = sess.cariboulite_sys.board_info.product_version;
    args["fpga_revision"] = std::to_string(1);
    args["vendor_name"] = sess.cariboulite_sys.board_info.product_vendor;
    args["product_name"] = sess.cariboulite_sys.board_info.product_name;

    return args;
}

/*******************************************************************
 * Antenna API
 ******************************************************************/
std::vector<std::string> Cariboulite::listAntennas( const int direction, const size_t channel ) const
{
    //printf("listAntennas dir: %d, channel: %ld\n", direction, channel);
	std::vector<std::string> options;
    if (radio.type == cariboulite_channel_s1g) options.push_back( "TX/RX Sub1GHz" );
    else if (radio.type == cariboulite_channel_6g) options.push_back( "TX/RX 6GHz" );
    
	return(options);
}

//========================================================
std::string Cariboulite::getAntenna( const int direction, const size_t channel ) const
{
    //printf("getAntenna dir: %d, channel: %ld\n", direction, channel);
	if (radio.type == cariboulite_channel_s1g) return "TX/RX Sub1GHz";
    else if (radio.type == cariboulite_channel_6g) return "TX/RX 6GHz";
    return "";
}

/*******************************************************************
 * Gain API
 ******************************************************************/
std::vector<std::string> Cariboulite::listGains(const int direction, const size_t channel) const
{
    //printf("listGains dir: %d, channel: %ld\n", direction, channel);
    std::vector<std::string> gains;
    if (direction == SOAPY_SDR_RX)
    {
        gains.push_back("Modem AGC");
    }
    else if (direction == SOAPY_SDR_TX)
    {
        gains.push_back("Modem PA");
    }

    return (gains);
}

//========================================================
/*!
* Set the overall amplification in a chain.
* The gain will be distributed automatically across available element.
* \param direction the channel direction RX or TX
* \param channel an available channel on the device
* \param value the new amplification value in dB
*/
void Cariboulite::setGain(const int direction, const size_t channel, const double value)
{
    //printf("setGain dir: %d, channel: %ld, value: %.2f\n", direction, channel, value);
    bool cur_agc_mode = radio.rx_agc_on;

    if (direction == SOAPY_SDR_RX)
    {
        cariboulite_radio_set_rx_gain_control(&radio, cur_agc_mode, value);
    }
    else if (direction == SOAPY_SDR_TX)
    {
        // base if -18dBm output so, given a gain of 0dB we should have -18 dBm
        cariboulite_radio_set_tx_power(&radio, value - 18.0);
    }
}

//========================================================
void Cariboulite::setGain(const int direction, const size_t channel, const std::string &name, const double value)
{
    // no amplification device names - redirect to  the regular setGain function
    Cariboulite::setGain(direction, channel, value);
}

//========================================================
double Cariboulite::getGain(const int direction, const size_t channel) const
{
    int value = 0;
   
    if (direction == SOAPY_SDR_RX)
    {
        cariboulite_radio_get_rx_gain_control((cariboulite_radio_state_st*)&radio, NULL, &value);
    }
    else if (direction == SOAPY_SDR_TX)
    {
        int temp = 0;
        cariboulite_radio_get_tx_power((cariboulite_radio_state_st*)&radio, &temp);
        value = temp + 18.0;
    }
    SoapySDR_logf(SOAPY_SDR_INFO, "getGain dir: %d, channel: %ld, value: %d", direction, channel, value);
    return (double)value;
}

//========================================================
double Cariboulite::getGain(const int direction, const size_t channel, const std::string &name) const
{
    return getGain(direction, channel);
}

//========================================================
SoapySDR::Range Cariboulite::getGainRange(const int direction, const size_t channel) const
{
    //printf("getGainRange dir: %d, channel: %ld\n", direction, channel);
    if (direction == SOAPY_SDR_RX)
    {
        return SoapySDR::Range(0.0, 23.0*3.0);
    }

    // and for TX
    return SoapySDR::Range(0.0, 31.0);
}

//========================================================
SoapySDR::Range Cariboulite::getGainRange(const int direction, const size_t channel, const std::string &name) const
{
    return getGainRange(direction, channel);
}


//========================================================
bool Cariboulite::hasGainMode(const int direction, const size_t channel) const
{
    //printf("hasGainMode dir: %d, channel: %ld\n", direction, channel);
    return (direction==SOAPY_SDR_RX?true:false);
}

//========================================================
/*!
* Set the automatic gain mode on the chain.
* \param direction the channel direction RX or TX
* \param channel an available channel on the device
* \param automatic true for automatic gain setting
*/
void Cariboulite::setGainMode( const int direction, const size_t channel, const bool automatic )
{
    //printf("setGainMode dir: %d, channel: %ld, auto: %d\n", direction, channel, automatic);
    bool rx_gain = radio.rx_gain_value_db;

    if (direction == SOAPY_SDR_RX)
    {
        cariboulite_radio_set_rx_gain_control(&radio, automatic, rx_gain);
    }
}

//========================================================
/*!
* Get the automatic gain mode on the chain.
* \param direction the channel direction RX or TX
* \param channel an available channel on the device
* \return true for automatic gain setting
*/
bool Cariboulite::getGainMode( const int direction, const size_t channel ) const
{
    bool mode = false;

    if (direction == SOAPY_SDR_RX)
    {
        cariboulite_radio_get_rx_gain_control((cariboulite_radio_state_st*)&radio, &mode, NULL);
        SoapySDR_logf(SOAPY_SDR_INFO, "getGainMode dir: %d, channel: %ld, auto: %d", direction, channel, mode);
        return mode;
    }
    
    return (false);
}

/*******************************************************************
 * Sample Rate API
 ******************************************************************/
void Cariboulite::setSampleRate( const int direction, const size_t channel, const double rate )
{
    at86rf215_radio_f_cut_en rx_cuttof = radio.rx_fcut;
    at86rf215_radio_f_cut_en tx_cuttof = radio.tx_fcut;

    //printf("setSampleRate dir: %d, channel: %ld, rate: %.2f\n", direction, channel, rate);
    if (direction == SOAPY_SDR_RX)
    {
        cariboulite_radio_set_rx_samp_cutoff(&radio, at86rf215_radio_rx_sample_rate_4000khz, rx_cuttof);
    }
    else if (direction == SOAPY_SDR_TX)
    {
        cariboulite_radio_set_tx_samp_cutoff(&radio, at86rf215_radio_rx_sample_rate_4000khz, tx_cuttof);
    }
}

//========================================================
double Cariboulite::getSampleRate( const int direction, const size_t channel ) const
{
    //printf("getSampleRate\n");
    // A single option for now. then we will add more options
    return 4000000;
}

//========================================================
std::vector<double> Cariboulite::listSampleRates( const int direction, const size_t channel ) const
{
    //printf("listSampleRates dir: %d, channel: %ld\n", direction, channel);
    std::vector<double> options;
	options.push_back( 4000000 );
    /*options.push_back( 2000000 ); // we want currently to allow only 4 MSPS to make the FPGA implementation easier
    options.push_back( 1333000 );
    options.push_back( 1000000 );
    options.push_back( 800000 );
    options.push_back( 666000 );
    options.push_back( 500000 );
    options.push_back( 400000 );*/
	return(options);
}

#define BW_SHIFT_FACT   (1.25)

//========================================================
static at86rf215_radio_rx_bw_en convertRxBandwidth(double bw_numeric)
{
    float fact = BW_SHIFT_FACT;
    if (fabs(bw_numeric - (160000*fact)) < 1) return at86rf215_radio_rx_bw_BW160KHZ_IF250KHZ;
    if (fabs(bw_numeric - (200000*fact)) < 1) return at86rf215_radio_rx_bw_BW200KHZ_IF250KHZ;
    if (fabs(bw_numeric - (250000*fact)) < 1) return at86rf215_radio_rx_bw_BW250KHZ_IF250KHZ;
    if (fabs(bw_numeric - (320000*fact)) < 1) return at86rf215_radio_rx_bw_BW320KHZ_IF500KHZ;
    if (fabs(bw_numeric - (400000*fact)) < 1) return at86rf215_radio_rx_bw_BW400KHZ_IF500KHZ;
    if (fabs(bw_numeric - (500000*fact)) < 1) return at86rf215_radio_rx_bw_BW500KHZ_IF500KHZ;
    if (fabs(bw_numeric - (630000*fact)) < 1) return at86rf215_radio_rx_bw_BW630KHZ_IF1000KHZ;
    if (fabs(bw_numeric - (800000*fact)) < 1) return at86rf215_radio_rx_bw_BW800KHZ_IF1000KHZ;
    if (fabs(bw_numeric - (1000000*fact)) < 1) return at86rf215_radio_rx_bw_BW1000KHZ_IF1000KHZ;
    if (fabs(bw_numeric - (1250000*fact)) < 1) return at86rf215_radio_rx_bw_BW1250KHZ_IF2000KHZ;
    if (fabs(bw_numeric - (1600000*fact)) < 1) return at86rf215_radio_rx_bw_BW1600KHZ_IF2000KHZ;
    if (fabs(bw_numeric - (2000000*fact)) < 1) return at86rf215_radio_rx_bw_BW2000KHZ_IF2000KHZ;

    //if (fabs(bw_numeric - (2500000*fact)) < 1) return at86rf215_radio_rx_bw_BW2000KHZ_IFCCKHZ;
    //if (fabs(bw_numeric - (3000000*fact)) < 1) return at86rf215_radio_rx_bw_BW2000KHZ_IFDDHZ;
    //if (fabs(bw_numeric - (4000000*fact)) < 1) return at86rf215_radio_rx_bw_BW2000KHZ_IFEEKHZ;
    //if (fabs(bw_numeric - (5000000*fact)) < 1) return at86rf215_radio_rx_bw_BW2000KHZ_IFFFKHZ;
    
    return at86rf215_radio_rx_bw_BW2000KHZ_IF2000KHZ;
}

//========================================================
static double convertRxBandwidth(at86rf215_radio_rx_bw_en bw_en)
{
    float fact = BW_SHIFT_FACT;
    if (at86rf215_radio_rx_bw_BW160KHZ_IF250KHZ == bw_en) return 160000 * fact;
    if (at86rf215_radio_rx_bw_BW200KHZ_IF250KHZ == bw_en) return 200000 * fact;
    if (at86rf215_radio_rx_bw_BW250KHZ_IF250KHZ == bw_en) return 250000 * fact;
    if (at86rf215_radio_rx_bw_BW320KHZ_IF500KHZ == bw_en) return 320000 * fact;
    if (at86rf215_radio_rx_bw_BW400KHZ_IF500KHZ == bw_en) return 400000 * fact;
    if (at86rf215_radio_rx_bw_BW500KHZ_IF500KHZ == bw_en) return 500000 * fact;
    if (at86rf215_radio_rx_bw_BW630KHZ_IF1000KHZ == bw_en) return 630000 * fact;
    if (at86rf215_radio_rx_bw_BW800KHZ_IF1000KHZ == bw_en) return 800000 * fact;
    if (at86rf215_radio_rx_bw_BW1000KHZ_IF1000KHZ == bw_en) return 1000000 * fact;
    if (at86rf215_radio_rx_bw_BW1250KHZ_IF2000KHZ == bw_en) return 1250000 * fact;
    if (at86rf215_radio_rx_bw_BW1600KHZ_IF2000KHZ == bw_en) return 1600000 * fact;
    if (at86rf215_radio_rx_bw_BW2000KHZ_IF2000KHZ == bw_en) return 2000000 * fact;

    //if (at86rf215_radio_rx_bw_BW2000KHZ_IFCCKHZ == bw_en) return 2500000 * fact;
    //if (at86rf215_radio_rx_bw_BW2000KHZ_IFDDHZ == bw_en) return 3000000 * fact;
    //if (at86rf215_radio_rx_bw_BW2000KHZ_IFEEKHZ == bw_en) return 4000000 * fact;
    //if (at86rf215_radio_rx_bw_BW2000KHZ_IFFFKHZ == bw_en) return 5000000 * fact;
    
    return 2000000 * fact;
}

//========================================================
static at86rf215_radio_tx_cut_off_en convertTxBandwidth(double bw_numeric)
{
    if (fabs(bw_numeric - 80000) < 1) return at86rf215_radio_tx_cut_off_80khz;
    if (fabs(bw_numeric - 100000) < 1) return at86rf215_radio_tx_cut_off_100khz;
    if (fabs(bw_numeric - 125000) < 1) return at86rf215_radio_tx_cut_off_125khz;
    if (fabs(bw_numeric - 160000) < 1) return at86rf215_radio_tx_cut_off_160khz;
    if (fabs(bw_numeric - 200000) < 1) return at86rf215_radio_tx_cut_off_200khz;
    if (fabs(bw_numeric - 250000) < 1) return at86rf215_radio_tx_cut_off_250khz;
    if (fabs(bw_numeric - 315000) < 1) return at86rf215_radio_tx_cut_off_315khz;
    if (fabs(bw_numeric - 400000) < 1) return at86rf215_radio_tx_cut_off_400khz;
    if (fabs(bw_numeric - 500000) < 1) return at86rf215_radio_tx_cut_off_500khz;
    if (fabs(bw_numeric - 625000) < 1) return at86rf215_radio_tx_cut_off_625khz;
    if (fabs(bw_numeric - 800000) < 1) return at86rf215_radio_tx_cut_off_800khz;
    if (fabs(bw_numeric - 1000000) < 1) return at86rf215_radio_tx_cut_off_1000khz;
    return at86rf215_radio_tx_cut_off_1000khz;
}

//========================================================
static double convertTxBandwidth(at86rf215_radio_tx_cut_off_en bw_en)
{
    if (at86rf215_radio_tx_cut_off_80khz == bw_en) return 80000;
    if (at86rf215_radio_tx_cut_off_100khz == bw_en) return 100000;
    if (at86rf215_radio_tx_cut_off_125khz == bw_en) return 125000;
    if (at86rf215_radio_tx_cut_off_160khz == bw_en) return 160000;
    if (at86rf215_radio_tx_cut_off_200khz == bw_en) return 200000;
    if (at86rf215_radio_tx_cut_off_250khz == bw_en) return 250000;
    if (at86rf215_radio_tx_cut_off_315khz == bw_en) return 315000;
    if (at86rf215_radio_tx_cut_off_400khz == bw_en) return 400000;
    if (at86rf215_radio_tx_cut_off_500khz == bw_en) return 500000;
    if (at86rf215_radio_tx_cut_off_625khz == bw_en) return 625000;
    if (at86rf215_radio_tx_cut_off_800khz == bw_en) return 800000;
    if (at86rf215_radio_tx_cut_off_1000khz == bw_en) return 1000000;
    return 1000000;
}


//========================================================
void Cariboulite::setBandwidth( const int direction, const size_t channel, const double bw )
{
	double modem_bw = bw;

    if (direction == SOAPY_SDR_RX)
    {
		if (modem_bw < (160000*BW_SHIFT_FACT) )
		{
			modem_bw = 160000*BW_SHIFT_FACT;
			if (bw <= 20000.0f) sample_queue_rx->setDigitalFilter(SoapySDR::Stream::DigitalFilter_20KHz);
			else if (bw <= 50000.0f) sample_queue_rx->setDigitalFilter(SoapySDR::Stream::DigitalFilter_50KHz);
			else if (bw <= 100000.0f) sample_queue_rx->setDigitalFilter(SoapySDR::Stream::DigitalFilter_100KHz);
			else sample_queue_rx->setDigitalFilter(SoapySDR::Stream::DigitalFilter_None);
		}
		else sample_queue_rx->setDigitalFilter(SoapySDR::Stream::DigitalFilter_None);

		cariboulite_radio_set_rx_bandwidth(&radio, convertRxBandwidth(modem_bw));
    }
    else if (direction == SOAPY_SDR_TX)
    {
        cariboulite_radio_set_tx_bandwidth(&radio, convertTxBandwidth(modem_bw));
    }
}

//========================================================
double Cariboulite::getBandwidth( const int direction, const size_t channel ) const
{
    //printf("getBandwidth dir: %d, channel: %ld\n", direction, channel);
    
    if (direction == SOAPY_SDR_RX)
    {
        at86rf215_radio_rx_bw_en bw;
        cariboulite_radio_get_rx_bandwidth((cariboulite_radio_state_st*)&radio, &bw);
        return convertRxBandwidth(bw);
    }
    else if (direction == SOAPY_SDR_TX)
    {
        at86rf215_radio_tx_cut_off_en bw;
        cariboulite_radio_get_tx_bandwidth((cariboulite_radio_state_st*)&radio, &bw);
        return convertTxBandwidth(bw);
    }
    return 0.0;
}

//========================================================
std::vector<double> Cariboulite::listBandwidths( const int direction, const size_t channel ) const
{
    //printf("listBandwidths\n");
    std::vector<double> options;
    if (direction == SOAPY_SDR_RX)
    {
        float fact = BW_SHIFT_FACT;
		options.push_back( 20000 );
		options.push_back( 50000 );
		options.push_back( 100000 );
        options.push_back( 160000*fact );
        options.push_back( 200000*fact );
        options.push_back( 250000*fact );
        options.push_back( 320000*fact );
        options.push_back( 400000*fact );
        options.push_back( 500000*fact );
        options.push_back( 630000*fact );
        options.push_back( 800000*fact );
        options.push_back( 1000000*fact );
        options.push_back( 1250000*fact );
        options.push_back( 1600000*fact );
        options.push_back( 2000000*fact );
    }
    else
    {
        options.push_back( 80000 );
        options.push_back( 100000 );
        options.push_back( 125000 );
        options.push_back( 160000 );
        options.push_back( 200000 );
        options.push_back( 250000 );
        options.push_back( 315000 );
        options.push_back( 400000 );
        options.push_back( 500000 );
        options.push_back( 625000 );
        options.push_back( 800000 );
        options.push_back( 1000000 );
    }
    return(options);
}

/*******************************************************************
 * Frequency API
 ******************************************************************/
void Cariboulite::setFrequency( const int direction, const size_t channel, const std::string &name, 
                                const double frequency, const SoapySDR::Kwargs &args )
{
    int err = 0;
    if (name != "RF")
    {
        return;
    }

    err = cariboulite_radio_set_frequency(&radio, true, (double *)&frequency);
    if (err == 0) SoapySDR_logf(SOAPY_SDR_INFO, "setFrequency dir: %d, channel: %ld, freq: %.2f", direction, channel, frequency);
    else SoapySDR_logf(SOAPY_SDR_ERROR, "setFrequency dir: %d, channel: %ld, freq: %.2f FAILED", direction, channel, frequency);
}

//========================================================
double Cariboulite::getFrequency( const int direction, const size_t channel, const std::string &name ) const
{
    //printf("getFrequency dir: %d, channel: %ld, name: %s\n", direction, channel, name.c_str());
    double freq;
    if (name != "RF")
    {
        return 0.0;
    }

    cariboulite_radio_get_frequency((cariboulite_radio_state_st*)&radio, &freq, NULL, NULL);
    return freq;
}

//========================================================
SoapySDR::ArgInfoList Cariboulite::getFrequencyArgsInfo(const int direction, const size_t channel) const
{
    //printf("getFrequencyArgsInfo\n");
    SoapySDR::ArgInfoList freqArgs;
	// TODO: frequency arguments
	return freqArgs;
}

//========================================================
std::vector<std::string> Cariboulite::listFrequencies( const int direction, const size_t channel ) const
{
    //printf("listFrequencies\n");
    // on both sub1ghz and the wide channel, the RF frequency is controlled
    std::vector<std::string> names;
	names.push_back( "RF" );
	return(names);
}

//========================================================
SoapySDR::RangeList Cariboulite::getFrequencyRange( const int direction, const size_t channel, const std::string &name ) const
{
    //printf("getFrequencyRange\n");
    if (name != "RF" )
    {
		throw std::runtime_error( "getFrequencyRange(" + name + ") unknown name" );
    }

    if (radio.type == cariboulite_channel_s1g)
    {
        SoapySDR::RangeList list;
        list.push_back(SoapySDR::Range( 389.5e6, 510e6 ));
        list.push_back(SoapySDR::Range( 779e6, 1020e6 ));
        return list;
    }
    else if (radio.type == cariboulite_channel_6g) 
    {
        return (SoapySDR::RangeList( 1, SoapySDR::Range( 1e6, 6000e6 ) ) );
    }
    throw std::runtime_error( "getFrequencyRange - unknown channel" );
}

