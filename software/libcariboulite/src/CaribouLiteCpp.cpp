#include <exception>
#include "CaribouLite.hpp"

std::shared_ptr<CaribouLite> CaribouLite::_instance = nullptr;
std::mutex CaribouLite::_instMutex;

//==================================================================
void CaribouLite::DefaultSignalHandler(void* context, int signal_number, siginfo_t *si)
{
    CaribouLite* cl = (CaribouLite*)context;
    std::cout << " >> Signal caught: " << signal_number << std::endl << std::flush;
    
    if (cl->_on_signal_caught) cl->_on_signal_caught(signal_number);
    //cl->ReleaseResources();
}

//==================================================================
void CaribouLite::RegisterSignalHandler(std::function<void(int)> on_signal_caught)
{
    _on_signal_caught = on_signal_caught;
}

//==================================================================
bool CaribouLite::DetectBoard(SysVersion *sysVer, std::string& name, std::string& guid)
{
    cariboulite_version_en hw_ver;
    char hw_name[64];
    char hw_guid[64];
    bool detected = cariboulite_detect_connected_board(&hw_ver, hw_name, hw_guid);
    if (detected)
    {
        name = std::string(hw_name);
        guid = std::string(hw_guid);
        if (sysVer) *sysVer = (SysVersion)hw_ver;
    }
    return detected;
}

//==================================================================
CaribouLite &CaribouLite::GetInstance(bool asyncApi, bool forceFpgaProg, LogLevel logLvl)
{
    SysVersion ver;
    std::string name, guid;
    if (!DetectBoard(&ver, name, guid))
    {
        throw std::runtime_error("CaribouLite was not detected");
    }
    
    std::lock_guard<std::mutex> lock(_instMutex);
    if (_instance == nullptr)
    {
        try
        {
            _instance = std::shared_ptr<CaribouLite>(new CaribouLite(asyncApi, forceFpgaProg, logLvl));
        }
        catch (std::exception& e)
        {
            throw e;
        }
    }
    return *_instance;
}

//==================================================================
CaribouLite::CaribouLite(bool asyncApi, bool forceFpgaProg, LogLevel logLvl)
{
    if (cariboulite_init(forceFpgaProg, (cariboulite_log_level_en)logLvl) != 0)
    {
        throw std::runtime_error("Driver initialization failed");
    }
    
    // register signal handler
    cariboulite_register_signal_handler (CaribouLite::DefaultSignalHandler, this);
    
    // get information
    DetectBoard(&_systemVersion, _productName, _productGuid);
    
    //printf("API TYPE: %d\n", asyncApi);
    CaribouLiteRadio::ApiType api_type = (asyncApi) ? CaribouLiteRadio::ApiType::Async : CaribouLiteRadio::ApiType::Sync;
    
    // populate the radio devices
    cariboulite_radio_state_st *radio_s1g = cariboulite_get_radio(cariboulite_channel_s1g);
    CaribouLiteRadio* radio_s1g_int = new CaribouLiteRadio(radio_s1g, CaribouLiteRadio::RadioType::S1G, api_type, this);
    _channels.push_back(radio_s1g_int);
    
    cariboulite_radio_state_st *radio_hif = cariboulite_get_radio(cariboulite_channel_hif);
    CaribouLiteRadio* radio_hif_int = new CaribouLiteRadio(radio_hif, CaribouLiteRadio::RadioType::HiF, api_type, this);
    _channels.push_back(radio_hif_int);
}

//==================================================================
void CaribouLite::ReleaseResources(void)
{
    if (!_instance) return;
    
    for (size_t i = 0; i < _instance->_channels.size(); i++)
    {
        if (_instance->_channels[i]) 
        {
            delete _instance->_channels[i];
        }
    }
    
    if (cariboulite_is_initialized()) 
    {
        cariboulite_close();
    }
}

//==================================================================
CaribouLite::~CaribouLite()
{
    if (_instance != nullptr) 
    {
        ReleaseResources();
        
        _instance.reset();
        _instance = nullptr;
    }
}

//==================================================================
bool CaribouLite::IsInitialized()
{
    return cariboulite_is_initialized();
}

//==================================================================
CaribouLiteVersion CaribouLite::GetApiVersion()
{
    cariboulite_lib_version_st v = {0};
    cariboulite_get_lib_version(&v);
    return CaribouLiteVersion(v.major_version, v.minor_version, v.revision);
}

//==================================================================
unsigned int CaribouLite::GetHwSerialNumber()
{
    return cariboulite_get_sn();
}

//==================================================================
CaribouLite::SysVersion CaribouLite::GetSystemVersion()
{
    return (CaribouLite::SysVersion)cariboulite_get_version();
}

//==================================================================
std::string CaribouLite::GetSystemVersionStr(CaribouLite::SysVersion v)
{
    switch(v)
    {
        case SysVersion::CaribouLiteFull: return std::string("CaribouLite6G"); break;
        case SysVersion::CaribouLiteISM: return std::string("CaribouLiteISM"); break;
        case SysVersion::Unknown: 
        default: return std::string("Unknown CaribouLite"); break;
    }
    return std::string(""); // unreachable.. hopefully
}

//==================================================================
std::string CaribouLite::GetSystemVersionStr(void)
{
    return CaribouLite::GetSystemVersionStr(GetSystemVersion());
}

//==================================================================
std::string CaribouLite::GetHwGuid(void)
{
    return _productGuid;
}

//==================================================================
CaribouLiteRadio* CaribouLite::GetRadioChannel(CaribouLiteRadio::RadioType ch)
{
    return _channels[(int)ch];
}
    
//==================================================================
void CaribouLite::SetLed0States (bool state)
{
    int led1 = 0;
    cariboulite_get_leds_state (NULL, &led1);
    cariboulite_set_leds_state (state, led1);
}

//==================================================================
bool CaribouLite::GetLed0States ()
{
    int led0 = 0;
    cariboulite_get_leds_state (&led0, NULL);
    return led0 != 0;
}

//==================================================================
void CaribouLite::SetLed1States (bool state)
{
    int led0 = 0;
    cariboulite_get_leds_state (&led0, NULL);
    cariboulite_set_leds_state (led0, state);

}

//==================================================================
bool CaribouLite::GetLed1States ()
{
    int led1 = 0;
    cariboulite_get_leds_state (NULL, &led1);
    return led1 != 0;
}

//==================================================================
bool CaribouLite::GetButtonState ()
{
    int btn = 0;
    cariboulite_get_button_state (&btn);
    return btn != 0;
}

//==================================================================
void CaribouLite::SetPmodState (uint8_t val)
{
    cariboulite_set_pmod_val (val);
}

//==================================================================
uint8_t CaribouLite::GetPmodState ()
{
    uint8_t val = 0;
    cariboulite_get_pmod_val (&val);
    return val;
}
