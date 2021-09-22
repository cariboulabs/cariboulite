#include "Cariboulite.hpp"
#include "cariboulite_config/cariboulite_config_default.h"

//========================================================
int soapy_sighandler(int signum)
{
    SoapySDR_logf(SOAPY_SDR_DEBUG, "Received signal %d", signum);
    switch (signum)
    {
        case SIGINT: printf("Caught SIGINT\n"); break;
        case SIGTERM: printf("Caught SIGTERM\n"); break;
        case SIGABRT: printf("Caught SIGABRT\n"); break;
        case SIGILL: printf("Caught SIGILL\n"); break;
        case SIGSEGV: printf("Caught SIGSEGV\n"); break;
        case SIGFPE:  printf("Caught SIGFPE\n"); break;
        default: printf("Caught Unknown Signal %d\n", signum); return -1; break;
    }

    return -1;
}

/*******************************************************************
 * Constructor / Destructor
 ******************************************************************/
Cariboulite::Cariboulite(const SoapySDR::Kwargs &args)
{
    CARIBOULITE_CONFIG_DEFAULT(temp);
    memcpy(&cariboulite_sys, &temp, sizeof(cariboulite_sys));
    cariboulite_init_driver(&cariboulite_sys, (void*)soapy_sighandler, NULL);
}

//========================================================
Cariboulite::~Cariboulite()
{
    printf("~Cariboulite\n");
    cariboulite_release_driver(&cariboulite_sys);
}

/*******************************************************************
 * Identification API
 ******************************************************************/
SoapySDR::Kwargs Cariboulite::getHardwareInfo() const
{
    printf("getHardwareInfo\n");
    //key/value pairs for any useful information
    //this also gets printed in --probe
    SoapySDR::Kwargs args;

    uint32_t serial_number = 0;
    uint32_t deviceId = 0;
    int count = 0;
    cariboulite_get_serial_number((cariboulite_st*)&cariboulite_sys, &serial_number, &count) ;

    args["device_id"] = std::to_string(deviceId);
    args["serial_number"] = std::to_string(serial_number);
    args["hardware_revision"] = cariboulite_sys.board_info.product_version;
    args["fpga_revision"] = std::to_string(1);
    args["vendor_name"] = cariboulite_sys.board_info.product_vendor;
    args["product_name"] = cariboulite_sys.board_info.product_name;

    return args;
}

/*******************************************************************
 * Antenna API
 ******************************************************************/
std::vector<std::string> Cariboulite::listAntennas( const int direction, const size_t channel ) const
{
    printf("listAntennas\n");
	std::vector<std::string> options;
    if (channel == cariboulite_channel_s1g) options.push_back( "TX/RX Sub1GHz" );
    else if (channel == cariboulite_channel_6g) options.push_back( "TX/RX 6GHz" );
    
	return(options);
}

//========================================================
std::string Cariboulite::getAntenna( const int direction, const size_t channel ) const
{
    printf("getAntenna\n");
	if (channel == cariboulite_channel_s1g) return "TX/RX Sub1GHz";
    else if (channel == cariboulite_channel_6g) return "TX/RX 6GHz";
    return "";
}

/*******************************************************************
 * Gain API
 ******************************************************************/
void Cariboulite::setGainMode( const int direction, const size_t channel, const bool automatic )
{
    printf("setGainMode\n");
	/* enable AGC if the hardware supports it, or remove this function */
}

//========================================================
bool Cariboulite::getGainMode( const int direction, const size_t channel ) const
{
    printf("getGainMode\n");
    /* AGC mode - enable AGC if the hardware supports it, or remove this function */
    return (false);
}

/*******************************************************************
 * Sample Rate API
 ******************************************************************/
void Cariboulite::setSampleRate( const int direction, const size_t channel, const double rate )
{
    printf("setSampleRate\n");
}

//========================================================
double Cariboulite::getSampleRate( const int direction, const size_t channel ) const
{
    printf("getSampleRate\n");
    return 4000000;
}

//========================================================
std::vector<double> Cariboulite::listSampleRates( const int direction, const size_t channel ) const
{
    printf("listSampleRates\n");
    std::vector<double> options;
	options.push_back( 4000000 );
    options.push_back( 2000000 );
    options.push_back( 1333000 );
    options.push_back( 1000000 );
    options.push_back( 800000 );
    options.push_back( 666000 );
    options.push_back( 500000 );
    options.push_back( 400000 );
	return(options);
}

//========================================================
void Cariboulite::setBandwidth( const int direction, const size_t channel, const double bw )
{
    printf("setBandwidth\n");
}

//========================================================
double Cariboulite::getBandwidth( const int direction, const size_t channel ) const
{
    printf("getBandwidth\n");
    return 2000000;
}

//========================================================
std::vector<double> Cariboulite::listBandwidths( const int direction, const size_t channel ) const
{
    printf("listBandwidths\n");
    std::vector<double> options;
    if (direction == SOAPY_SDR_RX)
    {
        options.push_back( 160000 );
        options.push_back( 200000 );
        options.push_back( 250000 );
        options.push_back( 320000 );
        options.push_back( 400000 );
        options.push_back( 500000 );
        options.push_back( 630000 );
        options.push_back( 800000 );
        options.push_back( 1000000 );
        options.push_back( 1250000 );
        options.push_back( 1600000 );
        options.push_back( 2000000 );
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
void Cariboulite::setFrequency( const int direction, const size_t channel, const std::string &name, const double frequency, const SoapySDR::Kwargs &args )
{
    printf("setFrequency\n");
}

//========================================================
double Cariboulite::getFrequency( const int direction, const size_t channel, const std::string &name ) const
{
    printf("getFrequency\n");
    return 900e6;
}

//========================================================
SoapySDR::ArgInfoList Cariboulite::getFrequencyArgsInfo(const int direction, const size_t channel) const
{
    printf("getFrequencyArgsInfo\n");
    SoapySDR::ArgInfoList freqArgs;
	// TODO: frequency arguments
	return freqArgs;
}

//========================================================
std::vector<std::string> Cariboulite::listFrequencies( const int direction, const size_t channel ) const
{
    printf("listFrequencies\n");
    // on both sub1ghz and the wide channel, the RF frequency is controlled
    std::vector<std::string> names;
	names.push_back( "RF" );
	return(names);
}

//========================================================
SoapySDR::RangeList Cariboulite::getFrequencyRange( const int direction, const size_t channel, const std::string &name ) const
{
    printf("getFrequencyRange\n");
    if (name != "RF" )
    {
		throw std::runtime_error( "getFrequencyRange(" + name + ") unknown name" );
    }

    if (channel == cariboulite_channel_s1g)
    {
        SoapySDR::RangeList list;
        list.push_back(SoapySDR::Range( 389.5e6, 510e6 ));
        list.push_back(SoapySDR::Range( 779e6, 1020e6 ));
        return list;
    }
    else if (channel == cariboulite_channel_6g) 
    {
        return (SoapySDR::RangeList( 1, SoapySDR::Range( 50e6, 6000e6 ) ) );
    }
    throw std::runtime_error( "getFrequencyRange - unknown channel" );
}

