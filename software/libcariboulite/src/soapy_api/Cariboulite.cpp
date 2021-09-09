#include "Cariboulite.hpp"

#include "cariboulite_config/cariboulite_config_default.h"

CARIBOULITE_CONFIG_DEFAULT(cariboulite_sys);

//========================================================
/*int soapy_sighandler(int signum)
{
    SoapySDR_logf(SOAPY_SDR_DEBUG, "Received signal %d", signum);
    switch (signum)
    {
        case SIGINT:
        case SIGTERM:
        case SIGABRT:
        case SIGILL:
        case SIGSEGV:
        case SIGFPE: stop_program (); break;
        default: return -1; break;
    }
    return 0;
}*/

//========================================================
Cariboulite::Cariboulite(const SoapySDR::Kwargs &args)
{
    cariboulite_init_driver(&cariboulite_sys, NULL);
}

//========================================================
Cariboulite::~Cariboulite()
{
    cariboulite_release_driver(&cariboulite_sys);
}

//========================================================
std::string Cariboulite::getDriverKey() const
{
    return "Cariboulite";
}

//========================================================
std::string Cariboulite::getHardwareKey() const
{
    return "Cariboulite Rev2";
}

//========================================================
SoapySDR::Kwargs Cariboulite::getHardwareInfo() const
{
    //key/value pairs for any useful information
    //this also gets printed in --probe
    SoapySDR::Kwargs args;

    uint32_t serial_number = 0;
    uint32_t deviceId = 0;
    int count = 0;
    cariboulite_get_serial_number(NULL, &serial_number, &count) ;

    args["device_id"] = std::to_string(deviceId);
    args["serial_number"] = std::to_string(serial_number);
    args["hardware_revision"] = std::to_string(2);
    args["fpga_revision"] = std::to_string(1);
    args["vendor_name"] = "Criboulabs.co";
    args["product_name"] = "Criboulite RPI HAT";

    return args;
}

//========================================================
size_t Cariboulite::getNumChannels(const int direction) const
{
    // the number of Tx/Rx channels in a single cariboulite
    return 2;
}

//========================================================
bool Cariboulite::getFullDuplex(const int direction, const size_t channel) const
{
    // any selected channel is a half duplex one.
    return 0;
}

SoapySDR::Kwargs Cariboulite::getChannelInfo(const int direction, const size_t channel) const
{
    SoapySDR::Kwargs args;
    
    args["duplex"] = "half";
    args["min_freq_hz"] = std::to_string(channel==0?             389e6  :  50e6);
    args["max_freq_hz"] = std::to_string(channel==0?            1020e6  :  6000e6);
    args["freq_res_hz"] = std::to_string(channel==0?              10.0  :  10.0);
    args["min_power_dbm"] = std::to_string(channel==0?           -30.0  :  -30.0);
    args["max_power_dbm"] = std::to_string(channel==0?            14.0  :  14.0);
    args["max_input_power_dbm"] = std::to_string(channel==0?       5.0  :  5.0);
    return args;
}

//========================================================
std::vector<std::string> Cariboulite::getStreamFormats(const int direction, const size_t channel) const
{
    std::vector<std::string> formats;
    formats.push_back("CS16");
    formats.push_back("CS12");
    formats.push_back("CS4");
    return formats;
}

//========================================================
std::string Cariboulite::getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const
{
    fullScale = (double)((1<<12)-1);
    return "CS16";
}

//========================================================
SoapySDR::ArgInfoList Cariboulite::getStreamArgsInfo(const int direction, const size_t channel) const
{
    SoapySDR::ArgInfoList list;
    return list;
}

//========================================================
SoapySDR::Stream *Cariboulite::setupStream(const int direction, 
                            const std::string &format, 
                            const std::vector<size_t> &channels, 
                            const SoapySDR::Kwargs &args)
{
    return NULL;
}

//========================================================
void Cariboulite::closeStream(SoapySDR::Stream *stream)
{

}

//========================================================
size_t Cariboulite::getStreamMTU(SoapySDR::Stream *stream) const
{
    return 1024;
}

//========================================================
int Cariboulite::activateStream(SoapySDR::Stream *stream,
                                    const int flags,
                                    const long long timeNs,
                                    const size_t numElems)
{
    return 0;
}

//========================================================
int Cariboulite::deactivateStream(SoapySDR::Stream *stream, const int flags, const long long timeNs)
{
    return 0;
}

//========================================================
int Cariboulite::readStream(
            SoapySDR::Stream *stream,
            void * const *buffs,
            const size_t numElems,
            int &flags,
            long long &timeNs,
            const long timeoutUs)
{
    return 0;
}