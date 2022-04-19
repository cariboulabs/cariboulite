#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <sstream>
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

    if ( ( count = cariboulite_config_detect_board(&board_info) ) <= 0)
    {
        SoapySDR_logf(SOAPY_SDR_DEBUG, "No Cariboulite boards found");
        return results;
    }
    
	// to support for two channels, each CaribouLite detected will be identified as
	// two boards for the SoapyAPI
    int devId = 0;
    for (int i = 0; i < count; i++) 
    {
		for (int ch = 0; ch < 2; ch ++)
		{
			std::stringstream serialstr;
			std::stringstream label;
			SoapySDR::Kwargs soapyInfo;
			serialstr.str("");
			serialstr << std::hex << ((board_info.numeric_serial_number << 1) | ch);
			label.str("");
			label << (ch?std::string("CaribouLite HiF"):std::string("CaribouLite S1G")) << "[" << serialstr.str() << "]";

			SoapySDR_logf(SOAPY_SDR_DEBUG, "Serial %s", serialstr.str().c_str());        

			soapyInfo["device_id"] = std::to_string(devId);
			soapyInfo["label"] = label.str();
			soapyInfo["serial"] = serialstr.str();
			soapyInfo["name"] = board_info.product_name;
			soapyInfo["vendor"] = board_info.product_vendor;
			soapyInfo["uuid"] = board_info.product_uuid;
			soapyInfo["version"] = board_info.product_version;
			soapyInfo["channel"] = ch?"HiF":"S1G";
			devId++;

			results.push_back(soapyInfo);
		}
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