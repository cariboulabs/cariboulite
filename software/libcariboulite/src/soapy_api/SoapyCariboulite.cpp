#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include "Cariboulite.hpp"


/***********************************************************************
 * Find available devices
 **********************************************************************/
SoapySDR::KwargsList findCariboulite(const SoapySDR::Kwargs &args)
{
    std::vector<SoapySDR::Kwargs> results;
    
    cariboulite_lib_version_st lib_version;
    cariboulite_lib_version(&lib_version);
    
    // SoapySDR_setLogLevel(SOAPY_SDR_DEBUG);
    
    SoapySDR_logf(SOAPY_SDR_DEBUG, "CaribouLite Lib v%d.%d rev %d", 
                lib_version.major_version, lib_version.minor_version, lib_version.revision);

    uint32_t serial_number[5] = {0};
    // When multiboard will be supported, change this to enumertion with count >1
    int count = 0;
    if (cariboulite_get_serial_number(NULL, serial_number, &count) != 0)
    {
        SoapySDR_logf(SOAPY_SDR_DEBUG, "No Cariboulite boards found");
        return results;
    }
    
    int devId = 0;
    
    for (int i = 0; i < count; i++) 
    {
        std::stringstream serialstr;
        
        serialstr.str("");
        serialstr << std::hex << serial_number[i];
        
        SoapySDR_logf(SOAPY_SDR_DEBUG, "Serial %s", serialstr.str().c_str());        

        SoapySDR::Kwargs soapyInfo;

        soapyInfo["device_id"] = std::to_string(devId);
        soapyInfo["label"] = "Cariboulite [" + serialstr.str() + "]";
        soapyInfo["serial"] = serialstr.str();
        devId++;

        if (args.count("device_id") != 0) {
            if (args.at("device_id") != soapyInfo.at("device_id")) {
                continue;
            }
            SoapySDR_logf(SOAPY_SDR_DEBUG, "Found device by device_id %s", soapyInfo.at("device_id").c_str());
        }
        
        results.push_back(soapyInfo);
    }
   
    return results;
}

/***********************************************************************
 * Make device instance
 **********************************************************************/
SoapySDR::Device *makeCariboulite(const SoapySDR::Kwargs &args)
{
    return new Cariboulite(args);
}

/***********************************************************************
 * Registration
 **********************************************************************/
static SoapySDR::Registry registerCariboulite("Cariboulite", &findCariboulite, &makeCariboulite, SOAPY_SDR_ABI_VERSION);