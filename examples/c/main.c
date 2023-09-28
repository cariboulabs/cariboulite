#include <stdio.h>
#include <cariboulite.h>

static cariboulite_lib_version_st version = {0};
static uint32_t serial_number = 0;
cariboulite_version_en hw_ver;
char hw_name[128];
char hw_uuid[128];

int main ()
{
    // board detection
    bool detected = cariboulite_detect_connected_board(&hw_ver, hw_name, hw_uuid);
    if (detected) printf("Detection: %d, HWVer: %d, HWName: %s, UUID: %s\n", detected, hw_ver, hw_name, hw_uuid);
    else 
    {
        printf("No board detection, Exiting\n");
        return 0;
    }
    
    // library version
    cariboulite_get_lib_version(&version);
    printf("Version: %02d.%02d.%02d\n", version.major_version, version.minor_version, version.revision);
    
    // init
	cariboulite_init(false, cariboulite_log_level_none);
    
    // board serial number
    serial_number = cariboulite_get_sn();
    printf("Serial Number: %08X\n", serial_number);
    
    // channels names and freqs
    char ch_name[64];
    int ch_num_ranges;
    float low_freq_vec[3];      // the actual size determined by ch_num_ranges but here we just statically allocated
    float high_freq_vec[3];
    int channels[2] = {cariboulite_channel_s1g, cariboulite_channel_hif};
    
    for (int ch_ind = 0; ch_ind < 2; ch_ind ++)
    {
        cariboulite_get_channel_name(channels[ch_ind], ch_name, sizeof(ch_name));
        ch_num_ranges = cariboulite_get_num_frequency_ranges(channels[ch_ind]);
        printf("Channel: %d, Name: %s, Num. Freq. Ranges: %d\n", channels[ch_ind], ch_name, ch_num_ranges);
        cariboulite_get_frequency_limits(channels[ch_ind], low_freq_vec, high_freq_vec, NULL);
        for (int i = 0; i < ch_num_ranges; i++)
        {       
            printf("   Range %d:  [%.2f, %.2f]\n", i, low_freq_vec[i], high_freq_vec[i]);
        }
    }
    
    cariboulite_close();
	return 0;
}
