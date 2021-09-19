#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include "Cariboulite.hpp"


/***********************************************************************
 * Find available devices
 **********************************************************************/
SoapySDR::KwargsList findCariboulite(const SoapySDR::Kwargs &args)
{
    int count = 0;
    cariboulite_board_info_st board_info;
    std::vector<SoapySDR::Kwargs> results;
    
    cariboulite_lib_version_st lib_version;
    cariboulite_lib_version(&lib_version);
    
    SoapySDR_logf(SOAPY_SDR_DEBUG, "CaribouLite Lib v%d.%d rev %d", 
                lib_version.major_version, lib_version.minor_version, lib_version.revision);

    // When multiboard will be supported, change this to enumertion with count >1
    
    if ( ( count = cariboulite_config_detect_board(&board_info) ) <= 0)
    {
        SoapySDR_logf(SOAPY_SDR_DEBUG, "No Cariboulite boards found");
        return results;
    }
    
    int devId = 0;
    for (int i = 0; i < count; i++) 
    {
        std::stringstream serialstr;
        
        serialstr.str("");
        serialstr << std::hex << board_info.numeric_serial_number;
        
        SoapySDR_logf(SOAPY_SDR_DEBUG, "Serial %s", serialstr.str().c_str());        

        SoapySDR::Kwargs soapyInfo;

        soapyInfo["device_id"] = std::to_string(devId);
        soapyInfo["label"] = "Cariboulite [" + serialstr.str() + "]";
        soapyInfo["serial"] = serialstr.str();
        soapyInfo["name"] = board_info.product_name;
        soapyInfo["vendor"] = board_info.product_vendor;
        soapyInfo["uuid"] = board_info.product_uuid;
        soapyInfo["version"] = board_info.product_version;
        devId++;

        if (args.count("device_id") != 0) 
        {
            if (args.at("device_id") != soapyInfo.at("device_id")) 
            {
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