#include <iostream>
#include <string>
#include <CaribouLite.hpp>
#include <thread>
#include <complex>

void printInfo(CaribouLite& cl)
{
    std::cout << "Initialized CaribouLite: " << cl.IsInitialized() << std::endl;
    std::cout << "API Versions: " << cl.GetApiVersion() << std::endl;
    std::cout << "Hardware Serial Number: " << std::hex << cl.GetHwSerialNumber() << std::endl;
    std::cout << "System Type: " << cl.GetSystemVersionStr() << std::endl;
    std::cout << "Hardware Unique ID: " << cl.GetHwGuid() << std::endl;
}

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

void receivedSamples(CaribouLiteRadio* radio, const std::complex<float>* samples, const bool* sync, size_t num_samples)
{
    std::cout << "Radio: " << radio->GetRadioName() << " Received " << num_samples << " samples" << std::endl;
}

int main ()
{
    detectBoard();
    CaribouLite &cl = CaribouLite::GetInstance();
    printInfo(cl);
        
    CaribouLiteRadio *s1g = cl.GetRadioChannel(CaribouLiteRadio::RadioType::S1G);
    CaribouLiteRadio *hif = cl.GetRadioChannel(CaribouLiteRadio::RadioType::HiF);
    
    std::cout << "First Radio Name: " << s1g->GetRadioName() << "  MtuSize: " << s1g->GetNativeMtuSample() << " Samples" << std::endl;
    std::cout << "First Radio Name: " << hif->GetRadioName() << "  MtuSize: " << hif->GetNativeMtuSample() << " Samples" << std::endl;
    
    s1g->SetFrequency(900000000);
    s1g->StartReceiving(receivedSamples, 20000);

    getchar();
    
    return 0;
}
