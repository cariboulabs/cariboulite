#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>
#include <sstream>
#include <iostream>
#include "Cariboulite.hpp"


/***********************************************************************
 * Find available devices
 **********************************************************************/
SoapySDR::KwargsList findCariboulite(const SoapySDR::Kwargs &args)
{
    int count = 0;
    hat_board_info_st board_info;
    std::vector<SoapySDR::Kwargs> results;

	std::cout << "Printing 'findCariboulite' Request:" << std::endl;
	for (auto const &pair: args) {
        std::cout << "    {" << pair.first << ": " << pair.second << "}\n";
    }

	// Library Version
    cariboulite_lib_version_st lib_version;
    cariboulite_lib_version(&lib_version);
    
    SoapySDR_logf(SOAPY_SDR_DEBUG, "CaribouLite Lib v%d.%d rev %d", 
                lib_version.major_version, lib_version.minor_version, lib_version.revision);

	// Detect CaribouLite board
    if ( ( count = hat_detect_board(&board_info) ) <= 0)
    {
        SoapySDR_logf(SOAPY_SDR_DEBUG, "No Cariboulite boards found");
        return results;
    }
    
	// to support for two channels, each CaribouLite detected will be identified as
	// two boards for the SoapyAPI
    int devId = 0;
    for (int i = 0; i < count; i++) 
    {
        // make sure its our board
        if (!strcmp(board_info.product_name, "CaribouLite RPI Hat") &&
            (board_info.numeric_product_id == system_type_cariboulite_full ||
             board_info.numeric_product_id == system_type_cariboulite_ism))
        {
            for (int ch = 0; ch < 2; ch ++)
            {
                SoapySDR::Kwargs soapyInfo;

                // Construct serial numbers and labels
                std::stringstream serialstr;
                std::stringstream label;
                serialstr << std::hex << ((board_info.numeric_serial_number << 1) | ch);
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
    }
   
	// no filterring specified, return all results
    if (args.count("serial") == 0 && 
		args.count("label") == 0 && 
		args.count("device_id") == 0 &&
		args.count("channel") == 0) return results;

	// filter the return according to the "serial" or "label" or "device_id"
    std::vector<SoapySDR::Kwargs> filteredResults;

	// requested device number (devNum)
    int req_dev_num = 			args.count("device_id") == 0 	? -1 : atoi(args.at("device_id").c_str());
	std::string req_serial = 	args.count("serial") == 0 		? "" : args.at("serial");
	std::string req_label = 	args.count("label") == 0		? "" : args.at("label");
	std::string req_channel = 	args.count("channel") == 0		? "" : args.at("channel");

	// search for the requested devNum within the un-filterred results
    for (size_t i = 0; i < results.size(); i++)
    {
		SoapySDR::Kwargs curArgs = results[i];
		int curDevNum = atoi(curArgs.at("device_id").c_str());
		std::string curSerial = curArgs.at("serial");
		std::string curLabel = curArgs.at("label");
		std::string curChannel = curArgs.at("channel");
		
		if (curDevNum == req_dev_num ||
			!curSerial.compare(req_serial) ||
			!curLabel.compare(req_label) ||
			!curChannel.compare(req_channel))
		{
			filteredResults.push_back(curArgs);
		}
    }

    return filteredResults;
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