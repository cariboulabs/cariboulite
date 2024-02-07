#include <CaribouLite.hpp>
#include <string.h>

//=================================================================
void CaribouLiteRadio::CaribouLiteRxThread(CaribouLiteRadio* radio)
{
    size_t mtu_size = radio->GetNativeMtuSample();
    std::complex<short>* rx_buffer = new std::complex<short>[mtu_size];
    CaribouLiteMeta* rx_meta_buffer = new CaribouLiteMeta[mtu_size];
    std::complex<float>* rx_copmlex_data = new std::complex<float>[mtu_size];
    
    //printf("Enterred Thread\n");
    
    while (radio->_rx_thread_running)
    {
        if (!radio->_rx_is_active)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }
        
        int ret = cariboulite_radio_read_samples((cariboulite_radio_state_st*)radio->_radio, 
                                                 (cariboulite_sample_complex_int16*)rx_buffer, 
                                                 (cariboulite_sample_meta*)rx_meta_buffer, 
                                                 radio->_rx_samples_per_chunk);
        if (ret < 0)
        {
            if (ret == -1)
            {
                //printf("reader thread failed to read SMI!\n");
            }
            ret = 0;
            continue;
        }
        if (ret == 0) continue;
        
        // convert the buffer
        if (radio->_rxCallbackType == CaribouLiteRadio::RxCbType::FloatSync || radio->_rxCallbackType == CaribouLiteRadio::RxCbType::Float)
        {
            for (int i = 0; i < ret; i ++)
            {
                rx_copmlex_data[i].real(rx_buffer[i].real() / 4096.0);
                rx_copmlex_data[i].imag(rx_buffer[i].imag() / 4096.0);
            }
        }
        
        // notify application
        try
        {
            switch(radio->_rxCallbackType)
            {
            case (CaribouLiteRadio::RxCbType::FloatSync): if (radio->_on_data_ready_fm) radio->_on_data_ready_fm(radio, rx_copmlex_data, rx_meta_buffer, ret); break;
            case (CaribouLiteRadio::RxCbType::Float): if (radio->_on_data_ready_f) radio->_on_data_ready_f(radio, rx_copmlex_data, ret); break;
            case (CaribouLiteRadio::RxCbType::IntSync): if (radio->_on_data_ready_im) radio->_on_data_ready_im(radio, rx_buffer, rx_meta_buffer, ret); break;
            case (CaribouLiteRadio::RxCbType::Int): if (radio->_on_data_ready_i) radio->_on_data_ready_i(radio, rx_buffer, ret); break;
            case (CaribouLiteRadio::RxCbType::None):
            default: break;
            }
        }
        catch (std::exception &e)
        {
            std::cout << "OnDataReady Exception: " << e.what() << std::endl;
        }
    }
    
    delete[]rx_buffer;
    delete[]rx_meta_buffer;
    delete[]rx_copmlex_data;
}

//==================================================================
int CaribouLiteRadio::ReadSamples(std::complex<float>* samples, size_t num_to_read, uint8_t* meta)
{
    if (samples == NULL)
    {
        printf("samples_is_null=%d", _read_samples==NULL);
        return 0;
    }        

    int ret = ReadSamples((std::complex<short>*)NULL,  num_to_read, meta);
    //printf("ret = %d\n", ret);
    if (ret <= 0)
    {
        return ret;
    }
    
    if (samples)
    {
        for (size_t i = 0; i < (size_t)ret; i++)
        {
            samples[i] = {((float)_read_samples[i].i) / 4096.0f, ((float)_read_samples[i].q) / 4096.0f};
        }
    }
    return ret;
}

//==================================================================
int CaribouLiteRadio::ReadSamples(std::complex<short>* samples, size_t num_to_read, uint8_t* meta)
{
    if (!_rx_is_active || _read_samples == NULL || _read_metadata == NULL || num_to_read == 0)
    {
        printf("reading from closed stream: rx_active = %d, _read_samples_is_null=%d, _read_metadata_is_null=%d, num_to_read=%ld\n",
            _rx_is_active, _read_samples==NULL, _read_metadata==NULL, num_to_read);
        return 0;
    }        
    
    int ret = cariboulite_radio_read_samples((cariboulite_radio_state_st*)_radio,
                                             _read_samples, 
                                             _read_metadata, 
                                             num_to_read);
    if (ret <= 0)
    {
        return ret;
    }
    
    if (samples)
    {
        for (size_t i = 0; i < (size_t)ret; i++)
        {
            samples[i] = {_read_samples[i].i, _read_samples[i].q};
        }
    }

    if (meta)
    {
        memcpy(meta, _read_metadata, (size_t)ret);
    }
    
    return ret;
}

//==================================================================
int CaribouLiteRadio::WriteSamples(std::complex<float>* samples, size_t num_to_write)
{
    return 0;
}

//==================================================================
int CaribouLiteRadio::WriteSamples(std::complex<short>* samples, size_t num_to_write)
{
    return cariboulite_radio_write_samples((cariboulite_radio_state_st*)_radio,
                            (cariboulite_sample_complex_int16*)samples,
                            num_to_write);
}

//==================================================================
void CaribouLiteRadio::CaribouLiteTxThread(CaribouLiteRadio* radio)
{
    while (radio->_tx_thread_running)
    {
        if (!radio->_tx_is_active)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
            continue;
        }
        // TBD
    }
}

//==================================================================
CaribouLiteRadio::CaribouLiteRadio( const cariboulite_radio_state_st* radio, 
                                    RadioType type, 
                                    ApiType api_type, 
                                    const CaribouLite* parent)                                    
            : _radio(radio), _device(parent), _type(type), _rxCallbackType(RxCbType::None), _api_type(api_type)
{
    if (_api_type == Async)
    {
        //printf("Creating Radio Type %d ASYNC\n", type);
        _rx_thread_running = true;
        _rx_thread = new std::thread(CaribouLiteRadio::CaribouLiteRxThread, this);
        
        _tx_thread_running = true;
        _tx_thread = new std::thread(CaribouLiteRadio::CaribouLiteTxThread, this);
    }
    else
    {
        //printf("Creating Radio Type %d SYNC\n", type);
        _read_samples = NULL;
        _read_metadata = NULL;
        size_t mtu_size = GetNativeMtuSample();
        // allocate internal buffers
        _read_samples = new cariboulite_sample_complex_int16[mtu_size];
        _read_metadata = new cariboulite_sample_meta[mtu_size];        
    }
}

//==================================================================
CaribouLiteRadio::~CaribouLiteRadio()
{
    //std::cout << "Destructor of Radio" << std::endl;
    StopReceiving();
    StopTransmitting();
    
    if (_api_type == Async)
    {
        _rx_thread_running = false;
        _rx_thread->join();
        if (_rx_thread) delete _rx_thread;
        
        _tx_thread_running = false;
        _tx_thread->join();
        if (_tx_thread) delete _tx_thread;
    }
    else
    {
        if (_read_samples) delete [] _read_samples;
        _read_samples = NULL;
        if (_read_metadata) delete [] _read_metadata;
        _read_metadata = NULL;
    }        
}    

// Gain
//==================================================================
void CaribouLiteRadio::SetAgc(bool agc_on)
{
    int gain = 0;
    bool temp = false;
    cariboulite_radio_get_rx_gain_control((cariboulite_radio_state_st*)_radio, &temp, &gain);
    cariboulite_radio_set_rx_gain_control((cariboulite_radio_state_st*)_radio, agc_on, gain);
}

//==================================================================
bool CaribouLiteRadio::GetAgc()
{
    int gain = 0;
    bool temp = false;
    cariboulite_radio_get_rx_gain_control((cariboulite_radio_state_st*)_radio, &temp, &gain);
    return temp;
}

//==================================================================
float CaribouLiteRadio::GetRxGainMin()
{
    int rx_min_gain_value_db, rx_max_gain_value_db, rx_gain_value_resolution_db;
    cariboulite_radio_get_rx_gain_limits((cariboulite_radio_state_st*)_radio, 
                                    &rx_min_gain_value_db,
                                    &rx_max_gain_value_db,
                                    &rx_gain_value_resolution_db);
                                    
    return rx_min_gain_value_db;
}

//==================================================================
float CaribouLiteRadio::GetRxGainMax()
{
    int rx_min_gain_value_db, rx_max_gain_value_db, rx_gain_value_resolution_db;
    cariboulite_radio_get_rx_gain_limits((cariboulite_radio_state_st*)_radio, 
                                    &rx_min_gain_value_db,
                                    &rx_max_gain_value_db,
                                    &rx_gain_value_resolution_db);
                                    
    return rx_max_gain_value_db;
}

//==================================================================
float CaribouLiteRadio::GetRxGainSteps()
{
    int rx_min_gain_value_db, rx_max_gain_value_db, rx_gain_value_resolution_db;
    cariboulite_radio_get_rx_gain_limits((cariboulite_radio_state_st*)_radio, 
                                    &rx_min_gain_value_db,
                                    &rx_max_gain_value_db,
                                    &rx_gain_value_resolution_db);
                                    
    return rx_gain_value_resolution_db;
}

//==================================================================
void CaribouLiteRadio::SetRxGain(float gain)
{
    bool temp = false;
    cariboulite_radio_get_rx_gain_control((cariboulite_radio_state_st*)_radio, &temp, NULL);
    cariboulite_radio_set_rx_gain_control((cariboulite_radio_state_st*)_radio, temp, (int)gain);
}

//==================================================================
float CaribouLiteRadio::GetRxGain()
{
    int igain = 0;
    cariboulite_radio_get_rx_gain_control((cariboulite_radio_state_st*)_radio, NULL, &igain);
    return (float)igain;
}

// Tx Power

//==================================================================
float CaribouLiteRadio::GetTxPowerMin()
{
    return -15.0f;
}

//==================================================================
float CaribouLiteRadio::GetTxPowerMax()
{
    return 18.0f;
}

//==================================================================
float CaribouLiteRadio::GetTxPowerStep()
{
    return 1.0f;
}

//==================================================================
void CaribouLiteRadio::SetTxPower(float pwr_dBm)
{
    cariboulite_radio_set_tx_power((cariboulite_radio_state_st*)_radio, (int)pwr_dBm);
}

//==================================================================
float CaribouLiteRadio::GetTxPower()
{
    int itx_pwr = 0;
    cariboulite_radio_get_tx_power((cariboulite_radio_state_st*)_radio, &itx_pwr);
    return (float)itx_pwr;
}

// Rx Bandwidth

//==================================================================
float CaribouLiteRadio::GetRxBandwidthMin()
{
    return 200000.0f;
}

//==================================================================
float CaribouLiteRadio::GetRxBandwidthMax()
{
    return 2500000.0f;
}

//==================================================================
void CaribouLiteRadio::SetRxBandwidth(float bw_hz)
{
    cariboulite_radio_set_rx_bandwidth_flt((cariboulite_radio_state_st*)_radio, bw_hz);
}

//==================================================================
float CaribouLiteRadio::GetRxBandwidth()
{
    float bw = 0;
    cariboulite_radio_get_rx_bandwidth_flt((cariboulite_radio_state_st*)_radio, &bw);
    return bw;
}

// Tx Bandwidth

//==================================================================
float CaribouLiteRadio::GetTxBandwidthMin()
{
    return 80000.0f;
}

//==================================================================
float CaribouLiteRadio::GetTxBandwidthMax()
{
    return 1000000.0f;
}

//==================================================================
void CaribouLiteRadio::SetTxBandwidth(float bw_hz)
{
    cariboulite_radio_set_tx_bandwidth_flt((cariboulite_radio_state_st*)_radio, bw_hz);
}

//==================================================================
float CaribouLiteRadio::GetTxBandwidth()
{
    float bw = 0;
    cariboulite_radio_get_tx_bandwidth_flt((cariboulite_radio_state_st*)_radio, &bw);
    return bw;
}

// Rx Sample Rate

//==================================================================
float CaribouLiteRadio::GetRxSampleRateMin()
{
    return 400000.0f;
}

//==================================================================
float CaribouLiteRadio::GetRxSampleRateMax()
{
    return 4000000.0f;
}

//==================================================================
void CaribouLiteRadio::SetRxSampleRate(float sr_hz)
{
    cariboulite_radio_set_rx_sample_rate_flt((cariboulite_radio_state_st*)_radio, sr_hz);
}

//==================================================================
float CaribouLiteRadio::GetRxSampleRate()
{
    float sr = 0.0f;
    cariboulite_radio_get_rx_sample_rate_flt((cariboulite_radio_state_st*)_radio, &sr);
    return sr;
}

// Tx Sample Rate

//==================================================================
float CaribouLiteRadio::GetTxSampleRateMin()
{
    return 400000.0f;
}

//==================================================================
float CaribouLiteRadio::GetTxSampleRateMax()
{
    return 4000000.0f;
}

//==================================================================
void CaribouLiteRadio::SetTxSampleRate(float sr_hz)
{
    cariboulite_radio_set_tx_samp_cutoff_flt((cariboulite_radio_state_st*)_radio, sr_hz);
}

//==================================================================
float CaribouLiteRadio::GetTxSampleRate()
{
    float sr = 0.0f;
    cariboulite_radio_get_tx_samp_cutoff_flt((cariboulite_radio_state_st*)_radio, &sr);
    return sr;
}

// RSSI and Rx Power and others

//==================================================================
float CaribouLiteRadio::GetRssi()
{
    float rssi = 0.0f;
    cariboulite_radio_get_rssi((cariboulite_radio_state_st*)_radio, &rssi);
    return rssi;
}

//==================================================================
float CaribouLiteRadio::GetEnergyDet()
{
    float ed = 0.0f;
    cariboulite_radio_get_energy_det((cariboulite_radio_state_st*)_radio, &ed);
    return ed;
}

//==================================================================
unsigned char CaribouLiteRadio::GetTrueRandVal()
{
    unsigned char val = 0;
    cariboulite_radio_get_rand_val((cariboulite_radio_state_st*)_radio, &val);
    return val;
}


// Frequency Control

//==================================================================
void CaribouLiteRadio::SetFrequency(float freq_hz)
{
    if (!cariboulite_frequency_available((cariboulite_channel_en)_type, freq_hz))
    {
        char msg[128] = {0};
        sprintf(msg, "Frequency out or range %.2f Hz on %s", freq_hz, GetRadioName().c_str());
        throw std::invalid_argument(msg);
    }
    double freq_dbl = freq_hz;
    if (cariboulite_radio_set_frequency((cariboulite_radio_state_st*)_radio, 
									true,
									&freq_dbl) != 0)
    {
        char msg[128] = {0};
        sprintf(msg, "Frequency setting on %s failed", GetRadioName().c_str());
        throw std::runtime_error(msg);
    }   
}

//==================================================================
float CaribouLiteRadio::GetFrequency()
{
    double freq = 0;
    cariboulite_radio_get_frequency((cariboulite_radio_state_st*)_radio, 
                                	&freq, NULL, NULL);
    return freq;
}

//==================================================================
std::vector<CaribouLiteFreqRange> CaribouLiteRadio::GetFrequencyRange()
{
    std::vector<CaribouLiteFreqRange> ranges;
    float freq_mins[10];
    float freq_maxs[10];
    int num = 0;
    cariboulite_get_frequency_limits((cariboulite_channel_en)_type, freq_mins, freq_maxs, &num);
    for (int i = 0; i < num; i++)
    {
        ranges.push_back(CaribouLiteFreqRange(freq_mins[i], freq_maxs[i]));
    }
    return ranges;
}

//==================================================================
float CaribouLiteRadio::GetFrequencyResolution()
{
    // TBD: todo calculate it according to the real hardware specs
    return 1.0f;
}

// Activation

//==================================================================
void CaribouLiteRadio::StartReceivingInternal(size_t samples_per_chunk)
{
    _rx_samples_per_chunk = (samples_per_chunk==0)?GetNativeMtuSample():samples_per_chunk;
    
    if (_rx_samples_per_chunk > GetNativeMtuSample())
    {
        _rx_samples_per_chunk = GetNativeMtuSample();
    }
    
    // make sure only one radio is receiving at once
    CaribouLiteRadio* otherRadio = ((CaribouLite*)_device)->GetRadioChannel((_type==RadioType::S1G)?(RadioType::HiF):(RadioType::S1G));
    otherRadio->StopReceiving();
    
    cariboulite_radio_activate_channel((cariboulite_radio_state_st*)_radio, cariboulite_channel_dir_rx, true);
    _rx_is_active = true;
}

//==================================================================
void CaribouLiteRadio::StartReceiving(std::function<void(CaribouLiteRadio*, const std::complex<float>*, CaribouLiteMeta*, size_t)> on_data_ready, size_t samples_per_chunk)
{
    if (_api_type == CaribouLiteRadio::ApiType::Sync)
    {
        StartReceiving();
        return;
    }
    _on_data_ready_fm = on_data_ready;
    _rxCallbackType = RxCbType::FloatSync;
    StartReceivingInternal(samples_per_chunk);
}

//==================================================================
void CaribouLiteRadio::StartReceiving(std::function<void(CaribouLiteRadio*, const std::complex<float>*, size_t)> on_data_ready, size_t samples_per_chunk)
{
    if (_api_type == CaribouLiteRadio::ApiType::Sync)
    {
        StartReceiving();
        return;
    }
    _on_data_ready_f = on_data_ready;
    _rxCallbackType = RxCbType::Float;
    StartReceivingInternal(samples_per_chunk);
}

//==================================================================
void CaribouLiteRadio::StartReceiving(std::function<void(CaribouLiteRadio*, const std::complex<short>*, CaribouLiteMeta*, size_t)> on_data_ready, size_t samples_per_chunk)
{
    if (_api_type == CaribouLiteRadio::ApiType::Sync)
    {
        StartReceiving();
        return;
    }
    _on_data_ready_im = on_data_ready;
    _rxCallbackType = RxCbType::IntSync;
    StartReceivingInternal(samples_per_chunk);
}

//==================================================================
void CaribouLiteRadio::StartReceiving(std::function<void(CaribouLiteRadio*, const std::complex<short>*, size_t)> on_data_ready, size_t samples_per_chunk)
{
    if (_api_type == CaribouLiteRadio::ApiType::Sync)
    {
        StartReceiving();
        return;
    }
    _on_data_ready_i = on_data_ready;
    _rxCallbackType = RxCbType::Int;
    StartReceivingInternal(samples_per_chunk);
}

//==================================================================
void CaribouLiteRadio::StartReceiving()
{
    _on_data_ready_im = nullptr;
    _rxCallbackType = RxCbType::None;
    StartReceivingInternal(0);
}

//==================================================================
void CaribouLiteRadio::StopReceiving()
{
    _rx_is_active = false;
    cariboulite_radio_activate_channel((cariboulite_radio_state_st*)_radio, cariboulite_channel_dir_rx, false);
}

//==================================================================
void CaribouLiteRadio::StartTransmitting(std::function<void(CaribouLiteRadio*, std::complex<float>*, const bool*, size_t*)> on_data_request, size_t samples_per_chunk)
{
    _rx_is_active = false;
    _on_data_request = on_data_request;
    _tx_samples_per_chunk = samples_per_chunk;
    cariboulite_radio_activate_channel((cariboulite_radio_state_st*)_radio, cariboulite_channel_dir_rx, false);
    cariboulite_radio_set_cw_outputs((cariboulite_radio_state_st*)_radio, false, false);
    cariboulite_radio_activate_channel((cariboulite_radio_state_st*)_radio, cariboulite_channel_dir_tx, true);
    _tx_is_active = true;
}

//==================================================================
void CaribouLiteRadio::StartTransmittingLo()
{
    _rx_is_active = false;
    _tx_is_active = false;
    cariboulite_radio_activate_channel((cariboulite_radio_state_st*)_radio, cariboulite_channel_dir_tx, false);
    cariboulite_radio_set_cw_outputs((cariboulite_radio_state_st*)_radio, true, false);
    cariboulite_radio_activate_channel((cariboulite_radio_state_st*)_radio, cariboulite_channel_dir_tx, true);
}

//==================================================================
void CaribouLiteRadio::StartTransmittingCw()
{
    _rx_is_active = false;
    _tx_is_active = false;
    cariboulite_radio_activate_channel((cariboulite_radio_state_st*)_radio, cariboulite_channel_dir_tx, false);
    cariboulite_radio_set_cw_outputs((cariboulite_radio_state_st*)_radio, false, true);
    cariboulite_radio_activate_channel((cariboulite_radio_state_st*)_radio, cariboulite_channel_dir_tx, true);
}

//==================================================================
void CaribouLiteRadio::StopTransmitting()
{
    _tx_is_active = false;
    cariboulite_radio_set_cw_outputs((cariboulite_radio_state_st*)_radio, false, false);
    cariboulite_radio_activate_channel((cariboulite_radio_state_st*)_radio, cariboulite_channel_dir_tx, false);
}

//==================================================================
bool CaribouLiteRadio::GetIsTransmittingLo()
{
    bool lo_out = false;
    cariboulite_radio_get_cw_outputs((cariboulite_radio_state_st*)_radio, &lo_out, NULL);
    return lo_out;
}

//==================================================================
bool CaribouLiteRadio::GetIsTransmittingCw()
{
    bool cw_out = false;
    cariboulite_radio_get_cw_outputs((cariboulite_radio_state_st*)_radio, NULL, &cw_out);
    return cw_out;
}

// General

//==================================================================
size_t CaribouLiteRadio::GetNativeMtuSample()
{
    return cariboulite_radio_get_native_mtu_size_samples((cariboulite_radio_state_st*)_radio);
}

//==================================================================
std::string CaribouLiteRadio::GetRadioName()
{
    char name[64];
    cariboulite_get_channel_name((cariboulite_channel_en)_type, name, sizeof(name));
    return std::string(name);
}

//==================================================================
void CaribouLiteRadio::FlushBuffers()
{
    int res = cariboulite_flush_pipeline();
    
}