#include <SoapySDR/Device.hpp>
#include <SoapySDR/Registry.hpp>

/***********************************************************************
 * Device interface
 **********************************************************************/
class CaribouLite : public SoapySDR::Device
{
    //Implement constructor with device specific arguments...

    //Implement all applicable virtual methods from SoapySDR::Device
};

/***********************************************************************
 * Find available devices
 **********************************************************************/
SoapySDR::KwargsList findCariboulite(const SoapySDR::Kwargs &args)
{
    //locate the device on the system...
    //return a list of 0, 1, or more argument maps that each identify a device
}

/***********************************************************************
 * Make device instance
 **********************************************************************/
SoapySDR::Device *makeCariboulite(const SoapySDR::Kwargs &args)
{
    //create an instance of the device object given the args
    //here we will translate args into something used in the constructor
    return new CaribouLite(...);
}

/***********************************************************************
 * Registration
 **********************************************************************/
static SoapySDR::Registry registerMyDevice("cariboulite", &findCariboulite, &makeCariboulite, SOAPY_SDR_ABI_VERSION);