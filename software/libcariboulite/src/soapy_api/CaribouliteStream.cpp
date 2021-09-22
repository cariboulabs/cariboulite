#include "Cariboulite.hpp"
#include "cariboulite_config/cariboulite_config_default.h"

//========================================================
std::vector<std::string> Cariboulite::getStreamFormats(const int direction, const size_t channel) const
{
    printf("getStreamFormats\n");
    std::vector<std::string> formats;
    formats.push_back("CS16");
	return formats;
}

//========================================================
std::string Cariboulite::getNativeStreamFormat(const int direction, const size_t channel, double &fullScale) const
{
    printf("getNativeStreamFormat\n");
    fullScale = (double)((1<<12)-1);
    return "CS16";
}

//========================================================
SoapySDR::ArgInfoList Cariboulite::getStreamArgsInfo(const int direction, const size_t channel) const
{
    printf("getStreamArgsInfo start\n");
	SoapySDR::ArgInfoList streamArgs;
	return streamArgs;
}

//========================================================
SoapySDR::Stream *Cariboulite::setupStream(const int direction, 
                            const std::string &format, 
                            const std::vector<size_t> &channels, 
                            const SoapySDR::Kwargs &args)
{
    printf("setupStream\n");
    return NULL;
}

//========================================================
void Cariboulite::closeStream(SoapySDR::Stream *stream)
{
    printf("closeStream\n");
}

//========================================================
size_t Cariboulite::getStreamMTU(SoapySDR::Stream *stream) const
{
    return 4096*4*10;       // 10 milliseconds of buffer
}

//========================================================
int Cariboulite::activateStream(SoapySDR::Stream *stream,
                                    const int flags,
                                    const long long timeNs,
                                    const size_t numElems)
{
    printf("activateStream\n");
    return 0;
}

//========================================================
int Cariboulite::deactivateStream(SoapySDR::Stream *stream, const int flags, const long long timeNs)
{
    printf("deactivateStream\n");
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
    printf("readStream\n");
    return 0;
}