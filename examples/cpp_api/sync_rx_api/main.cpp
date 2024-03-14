#include <iostream>
#include <string>
#include <CaribouLite.hpp>
#include <thread>
#include <complex>
#include <unistd.h>
#include <cmath>

// Print Board Information
void printInfo(CaribouLite& cl)
{
    std::cout << "Initialized CaribouLite: " << cl.IsInitialized() << std::endl;
    std::cout << "API Versions: " << cl.GetApiVersion() << std::endl;
    std::cout << "Hardware Serial Number: " << std::hex << cl.GetHwSerialNumber() << std::endl;
    std::cout << "System Type: " << cl.GetSystemVersionStr() << std::endl;
    std::cout << "Hardware Unique ID: " << cl.GetHwGuid() << std::endl;
}

// Detect the board before instantiating it
void detectBoard()
{
    CaribouLite::SysVersion ver;
    std::string name;
    std::string guid;

    if (CaribouLite::DetectBoard(&ver, name, guid))
    {
        std::cout << "Detected Version: " << CaribouLite::GetSystemVersionStr(ver) << ", Name: " << name << ", GUID: " << guid << std::endl;
    }
    else
    {
        std::cout << "Undetected CaribouLite!" << std::endl;
    }
}

// Calculate the RSSI
float RSSI(const std::complex<float>* signal, size_t num_of_samples)
{
    if (num_of_samples == 0)
    {
        return 0.0f;
    }

    float sum_of_squares = 0.0f;
    for (size_t i = 0; i < num_of_samples; ++i)
    {
        float vrmsp2 = (signal[i].real() * signal[i].real()) + (signal[i].imag() * signal[i].imag());
        sum_of_squares += vrmsp2 / 100.0;
    }

    float mean_of_squares = sum_of_squares / num_of_samples;

    // Convert RMS value to dBm
    return 10 * log10(mean_of_squares);
}

// Rx Callback (async)
void receivedSamples(CaribouLiteRadio* radio, const std::complex<float>* samples, CaribouLiteMeta* sync, size_t num_samples)
{
    std::cout << "Radio: " << radio->GetRadioName() << " Received " << std::dec << num_samples << " samples"
              << "RSSI: " << RSSI(samples, num_samples) << " dBm" << std::endl;

}


// Main entry
int main ()
{
    std::complex<float>* samples = new std::complex<float>[128*1024];

    // try detecting the board before getting the instance
    detectBoard();

    // get driver instance - use "CaribouLite&" rather than "CaribouLite" (ref)
    // get a "synchronous" api instance
    CaribouLite &cl = CaribouLite::GetInstance(false);

    // print the info after connecting
    printInfo(cl);

    // get the radios
    CaribouLiteRadio *s1g = cl.GetRadioChannel(CaribouLiteRadio::RadioType::S1G);
    CaribouLiteRadio *hif = cl.GetRadioChannel(CaribouLiteRadio::RadioType::HiF);

    // write radio information
    std::cout << "First Radio Name: " << s1g->GetRadioName() << "  MtuSize: " << std::dec << s1g->GetNativeMtuSample() << " Samples" << std::endl;
    std::cout << "First Radio Name: " << hif->GetRadioName() << "  MtuSize: " << std::dec << hif->GetNativeMtuSample() << " Samples" << std::endl;

    std::vector<CaribouLiteFreqRange> range_s1g = s1g->GetFrequencyRange();
    std::vector<CaribouLiteFreqRange> range_hif = hif->GetFrequencyRange();
    std::cout << "S1G Frequency Regions:" << std::endl;
    for (int i = 0; i < range_s1g.size(); i++)
    {
        std::cout << "  " << i << ": " << range_s1g[i] << std::endl;
    }

    std::cout << "HiF Frequency Regions:" << std::endl;
    for (int i = 0; i < range_hif.size(); i++)
    {
        std::cout << "  " << i << ": " << range_hif[i] << std::endl;
    }

    // Receive Synchrnonously
    try
    {
        hif->SetFrequency(2490000000);
    }
    catch (...)
    {
        std::cout << "The specified freq couldn't be used" << std::endl;
    }
    hif->SetRxGain(69);
    hif->SetAgc(false);
    hif->SetRxSampleRate(4000000);
    //hif->StartReceiving();

    s1g->SetRxGain(69);
    s1g->SetAgc(false);
    s1g->SetRxSampleRate(2000000);
    s1g->StartReceiving();

    int num_buffers = 10;
    while (num_buffers--)
    {
        //s1g->FlushBuffers();
        int ret = s1g->ReadSamples(samples, (1<<16));
        if (ret > 0)
        {
            std::cout << "Radio: " << hif->GetRadioName() << " Received " << std::dec << ret << " samples"
                      << "RSSI: " << RSSI(samples, ret) << " dBm" << std::endl;
            std::cout << "Radio: " << s1g->GetRadioName() << " Received " << std::dec << ret << " samples" << std::endl;
        }
    }

    /*try
    {
        s1g->SetFrequency(900100000);
    }
    catch (...)
    {
        std::cout << "The specified freq couldn't be used" << std::endl;
    }
    s1g->SetRxGain(69);
    s1g->SetRxBandwidth(2500e3);
    s1g->SetAgc(false);
    s1g->StartReceiving(receivedSamples);*/

    delete []samples;

    return 0;
}
