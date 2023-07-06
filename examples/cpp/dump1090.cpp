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
#include <chrono>
#include <csignal>
#include <chrono>
#include <thread>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <Iir.h>
#include "modes.h"


typedef struct
{
    int16_t i;
    int16_t q;
} complex_sample_16_t;

//=================================================================================
static void printBanner(void)
{
    std::cout << "######################################################" << std::endl;
    std::cout << "##       CaribouLite DUMP1090 - ADS-B Receiver      ##" << std::endl;
    std::cout << "######################################################" << std::endl;
    std::cout << std::endl;
}

//=================================================================================
static sig_atomic_t loopDone = false;
static void sigIntHandler(const int)
{
    loopDone = true;
}

//=================================================================================
void onModeSMessage(mode_s_t *self, struct mode_s_msg *mm) 
{
	mode_s_display_message(mm);
	printf("\n");
}

//=================================================================================
// Turn I/Q samples pointed by `data` into the magnitude vector pointed by `mag`
void CalculateMagnitudeVector(complex_sample_16_t *data, uint16_t *mag, uint32_t num_samples)
{
	uint32_t k;
	float i, q;
	int t = 0;
	for (k=0; k<num_samples; k+=1)
	{
		float i = (float)data[k].i;
		float q = (float)data[k].q;

		mag[k]=(uint16_t)sqrtf(i*i + q*q);
	}
}

//=================================================================================
void runSoapyProcess(	SoapySDR::Device *device, SoapySDR::Stream *stream, const size_t elemSize)
{
    // allocate buffers for the stream read/write
    const size_t numElems = device->getStreamMTU(stream);
    complex_sample_16_t* samples = (complex_sample_16_t*)malloc(sizeof(complex_sample_16_t)*numElems);
    uint16_t* mag = (uint16_t*)malloc(sizeof(uint16_t)*numElems);

    // MODE-S
    mode_s_t state;
    mode_s_init(&state);

    std::cout << "Starting stream loop, press Ctrl+C to exit..." << std::endl;
    device->activateStream(stream);
    signal(SIGINT, sigIntHandler);

    // Main Processing Loop
    while (not loopDone)
    {
        long long timeUS = numElems;
        int flags = 0;
        int numSamplesRead = device->readStream(stream, (void* const*)&samples, numElems, flags, timeUS);
        if (numSamplesRead < 0)
        {
            //std::cerr << "Unexpected stream error " << numSamplesRead << std::endl;
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        switch (numSamplesRead)
        {
            case SOAPY_SDR_TIMEOUT:
            case SOAPY_SDR_OVERFLOW: 
            case SOAPY_SDR_UNDERFLOW:
                continue;
            default:
                break;
        }

        // Proceed to DSP - compute the magnitude of the signal
        CalculateMagnitudeVector(samples, mag, numSamplesRead);

        // detect Mode S messages in the signal and call on_msg with each message
        mode_s_detect(&state, mag, numSamplesRead, onModeSMessage);
    }
    device->deactivateStream(stream);

    // free memory
    free(samples);
    free(mag);
}


/***********************************************************************
 * Main entry point
 **********************************************************************/
int main(int argc, char *argv[])
{
    SoapySDR::Device *device(nullptr);
    std::vector<size_t> channels;
    std::string argStr = "driver=Cariboulite,channel=HiF";
    double fullScale = 0.0;

    printBanner();

    try
    {
        device = SoapySDR::Device::make(argStr);
        if (device == NULL)
        {
            std::cerr << "Initialization Error" << std::endl;
            return EXIT_FAILURE;
        }

        // set the sample rate, frequency, ...
        device->setSampleRate(SOAPY_SDR_RX, 0, 2e6);    // needs to be sampled at 2MSPS
        device->setBandwidth(SOAPY_SDR_RX, 0, 200e5);
        device->setGainMode(SOAPY_SDR_RX, 0, false);
        device->setGain(SOAPY_SDR_RX, 0, 50);
        device->setFrequency(SOAPY_SDR_RX, 0, 1090e6);

        // create the stream, use the native format
        const auto format = device->getNativeStreamFormat(SOAPY_SDR_RX, 0, fullScale);
        const size_t elemSize = SoapySDR::formatToSize(format);
        auto stream = device->setupStream(SOAPY_SDR_RX, format, channels);

        // run the rate test one setup is complete
        std::cout << std::endl << "Running Soapy process with CaribouLite Config:" << std::endl;
        std::cout << "	Stream format: " << format << std::endl;
        std::cout << "	Channel: HiF" << std::endl;
        std::cout << "	Sample size: " << elemSize << " bytes" << std::endl;
        runSoapyProcess(device, stream, elemSize);

        // cleanup stream and device
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
