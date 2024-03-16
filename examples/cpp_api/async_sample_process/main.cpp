#include <iostream>
#include <string>
#include <thread>
#include <complex>
#include <cmath>

#include <CaribouLite.hpp>          // CPP API for CaribouLite
#include "circular_buffer.hpp"      // for circular sample buffer

#define SAMPLE_RATE         (4000000)
#define SAMPLE_RATE_CLOSEST (4194304)
#define MAX_FIFO_SECONDS    (2)
#define MAX_FIFO_SIZE       (SAMPLE_RATE_CLOSEST * MAX_FIFO_SECONDS)
#define TIME_BETWEEN_EPOCHS (5)
#define TIME_OF_SAMPLING    (1)
#define RX_CHUNK_SAMPLES    (8192)

// ==========================================================================================
// The Application context
// ==========================================================================================

typedef enum
{
    app_state_setup = 0,
    app_state_sampling = 1,
    app_state_sleeping = 2,
} appState_en;

typedef struct
{
    // device
    CaribouLite* cl;
    CaribouLiteRadio* radio;
    
    // operational parameters
    bool running;
    float freq;
    float gain;
    size_t num_samples_read_so_far;
    size_t epoch;
    appState_en state;
    bool requested_to_quit;
    
    // buffers & threads
    circular_buffer<std::complex<float>> *rx_fifo;
    std::thread *dsp_thread;
} appContext_st;

static appContext_st app = {0};

// ==========================================================================================
// General printing and detection of the board
// ==========================================================================================
void printInfo(CaribouLite& cl)
{
    std::cout << "Initialized CaribouLite: " << cl.IsInitialized() << std::endl;
    std::cout << "API Versions: " << cl.GetApiVersion() << std::endl;
    std::cout << "Hardware Serial Number: " << std::hex << cl.GetHwSerialNumber() << std::endl;
    std::cout << "System Type: " << cl.GetSystemVersionStr() << std::endl;
    std::cout << "Hardware Unique ID: " << cl.GetHwGuid() << std::endl;
}

// Detect boards
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

// Print radio information
void printRadioInformation(CaribouLiteRadio* radio)
{
    std::cout << "Radio Name: " << radio->GetRadioName() << "  MtuSize: " << std::dec << radio->GetNativeMtuSample() << " Samples" << std::endl;
    
    std::vector<CaribouLiteFreqRange> range = radio->GetFrequencyRange();
    std::cout << "Frequency Regions:" << std::endl;
    for (int i = 0; i < range.size(); i++)
    {
        std::cout << "  " << i << ": " << range[i] << std::endl;
    }
}

// ==========================================================================================
// DSP Thread - The thread that processes data whenever it is available and its
//              helper sub-functions
// ==========================================================================================

// Helper DSP function example: calculate the RSSI
float RSSI(const std::complex<float>* signal, size_t num_of_samples)
{
    if (num_of_samples == 0)
    {
        return 0.0f;
    }

    float sum_of_squares = 0.0f;
    for (size_t i = 0; i < num_of_samples && i < num_of_samples; ++i)
    {
        float vrms = std::norm(signal[i]);
        sum_of_squares += vrms * vrms / 100.0;
    }

    float mean_of_squares = sum_of_squares / num_of_samples;

    // Convert RMS value to dBm
    return 10 * log10(mean_of_squares);
}

// Consumer (parallel DSP) thread
void dataConsumerThread(appContext_st* app)
{
    std::cout << "Data consumer thread started" << std::endl;
    std::complex<float> local_dsp_buffer[RX_CHUNK_SAMPLES * 4];
    
    
    while (app->running)
    {
        // get the number of elements in the fifo
        size_t num_lements = app->rx_fifo->size();
        if (num_lements == 0) 
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        
        size_t actual_read = app->rx_fifo->get(local_dsp_buffer, (num_lements>(RX_CHUNK_SAMPLES*4)) ? (RX_CHUNK_SAMPLES * 4) : num_lements);
        if (actual_read)
        {
            // here goes the DSP
            app->num_samples_read_so_far += actual_read;
            float rssi = RSSI(local_dsp_buffer, actual_read);
            printf("DSP EPOCH: %ld, NUM_READ: %ld, RSSI: %.3f\n", app->epoch, app->num_samples_read_so_far, rssi);
        }
    }
    
    std::cout << "Data consumer thread exitting" << std::endl;
}

// ==========================================================================================
// Asynchronous API for receiving data and managing data flow in RX
// ==========================================================================================
// Rx Callback (async)
void receivedSamples(CaribouLiteRadio* radio, const std::complex<float>* samples, CaribouLiteMeta* sync, size_t num_samples)
{
    for (int i = 0; i < 6; i ++)
    {
        std::cout << "[" << samples[i].real() << ", " << samples[i].imag() << "]";
    }
    std::cout << std::endl;

    // push the received samples in the fifo
    app.rx_fifo->put(samples, num_samples);
}

// ==========================================================================================
// A simple asynchronous DSP flow
// ==========================================================================================
int main ()
{
    // try detecting the board before getting the instance
    detectBoard();
    CaribouLite &cl = CaribouLite::GetInstance();
    printInfo(cl);

    // get the radios
    CaribouLiteRadio *s1g = cl.GetRadioChannel(CaribouLiteRadio::RadioType::S1G);
    printRadioInformation(s1g);
    
    // create the application context
    app.cl = &cl;
    app.radio = s1g;
    app.freq = 900000000;
    app.gain = 69;
    app.rx_fifo = new circular_buffer<std::complex<float>>(MAX_FIFO_SIZE);
    app.num_samples_read_so_far = 0;
    app.state = app_state_sampling;
    app.epoch = 0;
    app.requested_to_quit = false;
    app.running = true;
    app.dsp_thread = new std::thread(dataConsumerThread, &app);
    
    // time management
    while (1)
    {
        switch(app.state)
        {
            //----------------------------------------------
            case app_state_setup:
                std::cout << "Starting Epoch " << app.epoch << std::endl;
                // an example periodic radio setup stage
                app.radio->SetRxGain(app.gain);
                app.radio->SetFrequency(app.freq);
                app.num_samples_read_so_far = 0;
                
                // and now go directly to the next sampling stage
                app.state = app_state_sampling;
            break;
            
            //----------------------------------------------
            case app_state_sampling:
                // start receiving
                app.radio->StartReceiving(receivedSamples, RX_CHUNK_SAMPLES);
                std::this_thread::sleep_for(std::chrono::milliseconds(TIME_OF_SAMPLING * 1000));
                
                // stop receiving
                app.radio->StopReceiving();
                app.state = app_state_sleeping;
            break;
            
            //----------------------------------------------
            case app_state_sleeping:
                std::this_thread::sleep_for(std::chrono::milliseconds(TIME_BETWEEN_EPOCHS * 1000));
                app.epoch ++;
                
                // reset the fifo
                app.rx_fifo->reset();
                
                // either go the next epoch or quit the program
                // this is an example flow but other possibilities exist
                if (!app.requested_to_quit) app.state = app_state_setup;
                else break;
            break;
            
            //----------------------------------------------
            default:
            break;
        }
    }
    
    // cleanup - done by the creator
    app.running = false;
    app.dsp_thread->join();
    delete app.dsp_thread;
    delete app.rx_fifo;
    
    return 0;
}
