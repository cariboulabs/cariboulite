/**
 * @file CaribouLite.hpp
 * @author David Michaeli
 * @date September 2023
 * @brief Main Init/Close API
 *
 * A high level CPP API for CaribouLite
 */
 
 
#ifndef __CARIBOULITE_HPP__
#define __CARIBOULITE_HPP__

#include <cariboulite.h>
#include <cariboulite_radio.h>

#include <vector>
#include <complex>
#include <cstddef>
#include <ostream>
#include <iostream>
#include <thread>
#include <memory>
#include <mutex>
#include <functional>

#if __cplusplus <= 199711L
  #error This file needs at least a C++11 compliant compiler, try using:
  #error    $ g++ -std=c++11 ..
#endif

/**
 * @brief CaribouLite API Version
 */
class CaribouLiteVersion
{
public:
    CaribouLiteVersion(int v, int sv, int rev) {_ver.major_version = v; _ver.minor_version = sv; _ver.revision = rev;}
    virtual ~CaribouLiteVersion() {}
    
    friend std::ostream& operator<<(std::ostream& os, const CaribouLiteVersion& c)
    {
        os << c._ver.major_version << '.' << c._ver.minor_version << '/' << c._ver.revision;
        return os;
    }
private:
    cariboulite_lib_version_st _ver;
};

/**
 * @brief CaribouLite Frequency Range
 */
class CaribouLiteFreqRange
{
public:
    CaribouLiteFreqRange(float min_hz, float max_hz) : _fMin(min_hz), _fMax(max_hz) {}
    virtual ~CaribouLiteFreqRange() {}
    friend std::ostream& operator<<(std::ostream& os, const CaribouLiteFreqRange& r)
    {
        os << "[" << r._fMin << ',' << r._fMax << ']';
        return os;
    }
private:
    float _fMin, _fMax;
};


/**
 * @brief CaribouLite Radio Channel
 */
#pragma pack(1)
struct CaribouLiteComplexInt
{
    int16_t i;
    int16_t q;
};

struct CaribouLiteMeta
{
    uint8_t sync;
};
#pragma pack()
 
class CaribouLite;
class CaribouLiteRadio
{
public:
    enum RadioType
    {
        S1G = 0,
        HiF = 1,
    };
    
    enum RadioDir
    {
        Rx = 0,
        Tx = 1,
    };
    
    enum RxCbType
    {
        None = 0,
        FloatSync = 1,
        Float = 2,
        IntSync = 3,
        Int = 4,
    };
    
    enum ApiType
    {
        Async = 0,
        Sync = 1,
    };

public:
    CaribouLiteRadio(const cariboulite_radio_state_st* radio, RadioType type, ApiType api_type = Async, const CaribouLite* parent = NULL);
    virtual ~CaribouLiteRadio();
    
    // Gain
    void SetAgc(bool agc_on);
    bool GetAgc(void);
    float GetRxGainMin(void);
    float GetRxGainMax(void);
    float GetRxGainSteps(void);
    void SetRxGain(float gain);
    float GetRxGain(void);
    
    // Tx Power
    float GetTxPowerMin(void);
    float GetTxPowerMax(void);
    float GetTxPowerStep(void);
    void SetTxPower(float pwr_dBm);
    float GetTxPower(void);
    
    // Rx Bandwidth
    float GetRxBandwidthMin(void);
    float GetRxBandwidthMax(void);
    void SetRxBandwidth(float bw_hz);
    float GetRxBandwidth(void);
    
    // Tx Bandwidth
    float GetTxBandwidthMin(void);
    float GetTxBandwidthMax(void);
    void SetTxBandwidth(float bw_hz);
    float GetTxBandwidth(void);
    
    // Rx Sample Rate
    float GetRxSampleRateMin(void);
    float GetRxSampleRateMax(void);
    void SetRxSampleRate(float sr_hz);
    float GetRxSampleRate(void);
    
    // Tx Sample Rate
    float GetTxSampleRateMin(void);
    float GetTxSampleRateMax(void);
    void SetTxSampleRate(float sr_hz);
    float GetTxSampleRate(void);
    
    // RSSI and Rx Power and others
    float GetRssi(void);
    float GetEnergyDet(void);
    unsigned char GetTrueRandVal(void);
    
    // Frequency Control
    void SetFrequency(float freq_hz);
    float GetFrequency(void);
    std::vector<CaribouLiteFreqRange> GetFrequencyRange(void);
    float GetFrequencyResolution(void);
    
    // Activation
    void StartReceiving(std::function<void(CaribouLiteRadio*, const std::complex<float>*, CaribouLiteMeta*, size_t)> on_data_ready, size_t samples_per_chunk = 0);
    void StartReceiving(std::function<void(CaribouLiteRadio*, const std::complex<float>*, size_t)> on_data_ready, size_t samples_per_chunk = 0);
    void StartReceiving(std::function<void(CaribouLiteRadio*, const std::complex<short>*, CaribouLiteMeta*, size_t)> on_data_ready, size_t samples_per_chunk = 0);
    void StartReceiving(std::function<void(CaribouLiteRadio*, const std::complex<short>*, size_t)> on_data_ready, size_t samples_per_chunk = 0);
    void StartReceiving();
    void StartReceivingInternal(size_t samples_per_chunk);
    void StopReceiving(void);
    void StartTransmitting(std::function<void(CaribouLiteRadio*, std::complex<float>*, const bool*, size_t*)> on_data_request, size_t samples_per_chunk);
    void StartTransmittingLo(void);
    void StartTransmittingCw(void);
    void StopTransmitting(void);
    bool GetIsTransmittingLo(void);
    bool GetIsTransmittingCw(void);
    
    // Synchronous Reading and Writing
    int ReadSamples(std::complex<float>* samples, size_t num_to_read);
    int ReadSamples(std::complex<short>* samples, size_t num_to_read);
    int WriteSamples(std::complex<float>* samples, size_t num_to_write);
    int WriteSamples(std::complex<short>* samples, size_t num_to_write);
    
    // General
    size_t GetNativeMtuSample(void);
    std::string GetRadioName(void);
    void FlushBuffers(void);
    
private:
    const cariboulite_radio_state_st* _radio;
    const CaribouLite* _device;
    const RadioType _type;
    
    bool _rx_thread_running;
    bool _rx_is_active;
    std::thread *_rx_thread;
    std::function<void(CaribouLiteRadio*, const std::complex<float>*, CaribouLiteMeta*, size_t)> _on_data_ready_fm;
    std::function<void(CaribouLiteRadio*, const std::complex<float>*, size_t)> _on_data_ready_f;
    std::function<void(CaribouLiteRadio*, const std::complex<short>*, CaribouLiteMeta*, size_t)> _on_data_ready_im;
    std::function<void(CaribouLiteRadio*, const std::complex<short>*, size_t)> _on_data_ready_i;
    size_t _rx_samples_per_chunk;
    RxCbType _rxCallbackType;
    ApiType _api_type;
    
    bool _tx_thread_running;
    bool _tx_is_active;
    std::thread *_tx_thread;
    std::function<void(CaribouLiteRadio*, std::complex<float>*, const bool*, size_t*)> _on_data_request;
    size_t _tx_samples_per_chunk;
    
    // buffers
    cariboulite_sample_complex_int16 *_read_samples;
    cariboulite_sample_meta* _read_metadata;
    
private:
    static void CaribouLiteRxThread(CaribouLiteRadio* radio);
    static void CaribouLiteTxThread(CaribouLiteRadio* radio);
};

/**
 * @brief CaribouLite Device
 */
class CaribouLite
{
public:
    enum LogLevel
    {
        Verbose,
        Info,
        None,
    };
    
    enum SysVersion
    {
        Unknown = 0,
        CaribouLiteFull = 1,
        CaribouLiteISM = 2,
    };
    
protected:
    CaribouLite(bool asyncApi = true, bool forceFpgaProg = false, LogLevel logLvl = LogLevel::None);
    CaribouLite(const CaribouLite& o) = delete;
    void operator=(const CaribouLite&) = delete;
    void ReleaseResources(void);
    
public:
    ~CaribouLite();
    bool IsInitialized(void);
    CaribouLiteVersion GetApiVersion(void);
    unsigned int GetHwSerialNumber(void);
    SysVersion GetSystemVersion(void);
    std::string GetSystemVersionStr(void);
    static std::string GetSystemVersionStr(SysVersion v);
    std::string GetHwGuid(void);
    CaribouLiteRadio* GetRadioChannel(CaribouLiteRadio::RadioType ch);
    
    // Signal Handler
    void RegisterSignalHandler(std::function<void(int)> on_signal_caught);
    
    // Static detection and factory
    static CaribouLite &GetInstance(bool asyncApi = true, bool forceFpgaProg = false, LogLevel logLvl = LogLevel::None);
    static bool DetectBoard(SysVersion *sysVer, std::string& name, std::string& guid);
    static void DefaultSignalHandler(void* context, int signal_number, siginfo_t *si);
    
    // IO Control
    void SetLed0States (bool state);
    bool GetLed0States (void);
    void SetLed1States (bool state);
    bool GetLed1States (void);
    bool GetButtonState (void);
    void SetPmodState (uint8_t val);
    uint8_t GetPmodState (void);
    
private:
    std::function<void(int)> _on_signal_caught;
    std::vector<CaribouLiteRadio*> _channels;
    SysVersion _systemVersion;
    std::string _productName;
    std::string _productGuid;
    
    static std::shared_ptr<CaribouLite> _instance;
    static std::mutex _instMutex;
};

#endif // __CARIBOULITE_HPP__