#include "Cariboulite.hpp"

//========================================================
std::vector<std::string> Cariboulite::listSensors(const int direction, const size_t channel) const
{
    std::vector<std::string> lst;

	if (direction == SOAPY_SDR_RX) lst.push_back( "RSSI" );
    if (direction == SOAPY_SDR_RX) lst.push_back( "ENERGY" );
    lst.push_back( "PLL_LOCK_MODEM" );
    if (channel == cariboulite_channel_6g)
    {
        lst.push_back( "PLL_LOCK_MIXER" );
    }

	return(lst);
}

//========================================================
SoapySDR::ArgInfo Cariboulite::getSensorInfo(const int direction, const size_t channel, const std::string &key) const
{
    SoapySDR::ArgInfo info;
    if (direction == SOAPY_SDR_RX)
    {
        if (key == "RSSI")
        {
            info.name = "RX RSSI";
            info.key = "RSSI";
            info.type = info.FLOAT;
            info.description = "Modem level RSSI measurment";
            info.range = SoapySDR::Range(-127, 4);
            return info;
        }
        if (key == "ENERGY")
        {
            info.name = "RX ENERGY";
            info.key = "ENERGY";
            info.type = info.FLOAT;
            info.description = "Modem level ENERGY (EDC) measurment";
            info.range = SoapySDR::Range(-127, 4);
            return info;
        }
    }

    if (key == "PLL_LOCK_MODEM")
    {
        info.name = "PLL Lock Modem";
        info.key = "PLL_MODEM";
        info.type = info.BOOL;
        info.description = "Modem PLL locking indication";
        return info;
    }

    if (channel == cariboulite_channel_6g && key == "PLL_LOCK_MIXER")
    {
        info.name = "PLL Lock Mixer";
        info.key = "PLL_MIXER";
        info.type = info.BOOL;
        info.description = "Mixer LO PLL locking indication";
        return info;
    }
    return info;
}

//========================================================
std::string Cariboulite::readSensor(const int direction, const size_t channel, const std::string &key) const
{
    return std::to_string(readSensor<float>(direction, channel, key));
}

//========================================================
template <typename Type>
Type Cariboulite::readSensor(const int direction, const size_t channel, const std::string &key) const
{
    if (direction == SOAPY_SDR_RX)
    {
        if (key == "RSSI")
        {
            float rssi = 0.0f;
            cariboulite_get_rssi((cariboulite_radios_st*)&radios,(cariboulite_channel_en)channel, &rssi);
            return rssi;
        }
        if (key == "ENERGY")
        {
            float energy = 0.0f;
            cariboulite_get_energy_det((cariboulite_radios_st*)&radios,(cariboulite_channel_en)channel, &energy);
            return energy;
        }
    }

    cariboulite_radio_state_st* rad = (cariboulite_radio_state_st* )
                                ((channel == cariboulite_channel_s1g) ? &radios.radio_sub1g : &radios.radio_6g);

    if (key == "PLL_LOCK_MODEM")
    {
        return rad->modem_pll_locked;
    }

    if (channel == cariboulite_channel_6g && key == "PLL_LOCK_MIXER")
    {
        return rad->lo_pll_locked;
    }
    return 0;
}