#include <SoapySDR/Version.hpp>
#include <SoapySDR/Modules.hpp>
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Device.hpp>
#include <SoapySDR/ConverterRegistry.hpp>

#include <algorithm>
#include <cstdlib>
#include <cstddef>
#include <iostream>
#include <iomanip>
#include <csignal>
#include <chrono>
#include <thread>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <Iir.h>
#include "modes.h"


/***********************************************************************
 * Print the banner
 **********************************************************************/
static void printBanner(void)
{
    std::cout << "######################################################" << std::endl;
    std::cout << "##       CaribouLite DUMP1090 - ADS-B Receiver      ##" << std::endl;
    std::cout << "######################################################" << std::endl;
    std::cout << std::endl;
}

/***********************************************************************
 * Find devices and print args
 **********************************************************************/
static int findDevices(const std::string &argStr, const bool sparse)
{
    const auto results = SoapySDR::Device::enumerate(argStr);
    std::cout << "Found " << results.size() << " devices" << std::endl;

    for (size_t i = 0; i < results.size(); i++)
    {
        std::cout << "Found device " << i << std::endl;
        for (const auto &it : results[i])
        {
            std::cout << "  " << it.first << " = " << it.second << std::endl;
        }
        std::cout << std::endl;
    }
    if (results.empty()) std::cerr << "No devices found! " << argStr << std::endl;
    else std::cout << std::endl;

    return results.empty()?EXIT_FAILURE:EXIT_SUCCESS;
}

/***********************************************************************
 * Run the stream function
 **********************************************************************/
static sig_atomic_t loopDone = false;
static void sigIntHandler(const int)
{
    loopDone = true;
}

void onModeSMessage(mode_s_t *self, struct mode_s_msg *mm) 
{
	printf("Got message from flight %s at altitude %d\n", mm->flight, mm->altitude);
}

#define FILT_ORDER	6
Iir::Butterworth::LowPass<FILT_ORDER> filt;

void makeFilter(double fs, double cutoff)
{
	try {filt.setup(fs, cutoff);}
	catch (...) {printf("Filter Setup Exception!\n");}
}

// Turn I/Q samples pointed by `data` into the magnitude vector pointed by `mag`
void MagnitudeVector(short *data, uint16_t *mag, uint32_t size)
{
	uint32_t k;
	float ii, qq, i, q;
	int t = 0;
	for (k=0; k<size; k+=2, t++)
	{
		float i = data[k];
		float q = data[k+1];

		//ii = filt.filter(i);
		//qq = filt.filter(q);

		mag[t]=(uint16_t)sqrt(ii*ii + qq*qq);
	}
}

void runSoapyProcess(
    SoapySDR::Device *device,
    SoapySDR::Stream *stream,
    const int direction,
    const size_t numChans,
    const size_t elemSize)
{
    //allocate buffers for the stream read/write
    const size_t numElems = device->getStreamMTU(stream);
	int16_t* buff = (int16_t*)malloc (2*sizeof(int16_t)*numElems);	// complex 16 bit samples
	uint16_t* mag = (uint16_t*)malloc(sizeof(uint16_t)*numElems);

	// MODE-S Stuff
	mode_s_t state;
	mode_s_init(&state);
	makeFilter(4e6, 50e3);

    std::cout << "Starting stream loop, press Ctrl+C to exit..." << std::endl;
    device->activateStream(stream);
    signal(SIGINT, sigIntHandler);
    while (not loopDone)
    {
        int ret = 0;
        int flags = 0;
        long long timeUS = numElems;
        ret = device->readStream(stream, (void* const*)&buff, numElems, flags, timeUS);

        if (ret == SOAPY_SDR_TIMEOUT) continue;
        if (ret == SOAPY_SDR_OVERFLOW)
        {
            //overflows++;
            continue;
        }
        if (ret == SOAPY_SDR_UNDERFLOW)
        {
            //underflows++;
            continue;
        }
        if (ret < 0)
        {
            std::cerr << "Unexpected stream error " << ret << std::endl;
            break;
        }

        // compute the magnitude of the signal
		MagnitudeVector(buff, mag, ret);

		for (int jjj = 0; jjj < numElems; jjj++)
		{
			printf("%d, ", buff[jjj*2]);
		}

		// detect Mode S messages in the signal and call on_msg with each message
		mode_s_detect(&state, mag, ret, onModeSMessage);

    }
    device->deactivateStream(stream);
}


/***********************************************************************
 * Main entry point
 **********************************************************************/
int main(int argc, char *argv[])
{
    SoapySDR::ModuleManager mm(false);
    SoapySDR::Device *device(nullptr);
    std::vector<size_t> channels;
    std::string argStr;
    double fullScale = 0.0;
	double freq = 1090.0e6;
	//double freq = 1090e6;

    printBanner();
    //findDevices(argStr, false);

    try
    {
        device = SoapySDR::Device::make(argStr);

        // push the 6GHZ channel into the channel list
        channels.push_back(1);

        // set the sample rate
        device->setSampleRate(SOAPY_SDR_RX, channels[0], 4e6);
 		device->setBandwidth(SOAPY_SDR_RX, channels[0], 2.5e6);
		device->setGainMode(SOAPY_SDR_RX, channels[0], false);
		device->setGain(SOAPY_SDR_RX, channels[0], 60);
		device->setFrequency(SOAPY_SDR_RX, channels[0], freq);

        //create the stream, use the native format   
        const auto format = device->getNativeStreamFormat(SOAPY_SDR_RX, channels.front(), fullScale);
        const size_t elemSize = SoapySDR::formatToSize(format);
        auto stream = device->setupStream(SOAPY_SDR_RX, format, channels);

        //run the rate test one setup is complete
        std::cout << "Stream format: " << format << std::endl;
        std::cout << "Num channels: " << channels.size() << std::endl;
        std::cout << "Element size: " << elemSize << " bytes" << std::endl;
        runSoapyProcess(device, stream, SOAPY_SDR_RX, channels.size(), elemSize);

        //cleanup stream and device
        device->closeStream(stream);
        SoapySDR::Device::unmake(device);
    }
    catch (const std::exception &ex)
    {
        std::cerr << "Error " << ex.what() << std::endl;
        SoapySDR::Device::unmake(device);
        return EXIT_FAILURE;
    }
    std::cout << std::endl;


    return 0;
}
