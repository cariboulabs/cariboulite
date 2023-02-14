#ifndef __CARIBOULITE_CONFIG_DEFAULT_H__
#define __CARIBOULITE_CONFIG_DEFAULT_H__

#ifdef __cplusplus
extern "C" {
#endif

// PINOUT SPI
#define CARIBOULITE_SPI_DEV 1
#define CARIBOULITE_MOSI 20
#define CARIBOULITE_SCK 21
#define CARIBOULITE_MISO 19

// PINOUT FPGA
#define CARIBOULITE_FPGA_SPI_CHANNEL 0
#define CARIBOULITE_FPGA_SS 18
#define CARIBOULITE_FPGA_CDONE 27
#define CARIBOULITE_FPGA_CRESET 26
#define CARIBOULITE_FPGA_SOFT_RESET 4

// PINOUT AT86 - AT86RF215
#define CARIBOULITE_MODEM_SPI_CHANNEL 1
#define CARIBOULITE_MODEM_SS 17
#define CARIBOULITE_MODEM_IRQ 22
#define CARIBOULITE_MODEM_RESET 23

// PINOUT MIXER - RFFC5072
#define CARIBOULITE_MIXER_SPI_CHANNEL 2
#define CARIBOULITE_MIXER_SS 16
#define CARIBOULITE_MIXER_RESET 5

//=======================================================================================
// SYSTEM DEFINITIONS & CONFIGURATIONS
//=======================================================================================
#define CARIBOULITE_CONFIG_DEFAULT(a)                                   \
                sys_st(a)={                                     		\
                    .board_info = {0},                                  \
                    .spi_dev =                                          \
                    {                                                   \
                        .miso = CARIBOULITE_MISO,                       \
                        .mosi = CARIBOULITE_MOSI,                       \
                        .sck = CARIBOULITE_SCK,                         \
                        .initialized = 0,                               \
                    },                                                  \
                    .smi =                                              \
                    {                                                   \
                        .initialized = 0,                               \
                    },                                                  \
                    .timer =                                            \
                    {                                                   \
                        .initialized = 0,                               \
                    },                                                  \
                    .fpga =                                             \
                    {                                                   \
                        .reset_pin = CARIBOULITE_FPGA_CRESET,           \
						.soft_reset_pin = CARIBOULITE_FPGA_SOFT_RESET,	\
                        .cs_pin = CARIBOULITE_FPGA_SS,                  \
                        .spi_dev = CARIBOULITE_SPI_DEV,                 \
                        .spi_channel = CARIBOULITE_FPGA_SPI_CHANNEL,    \
                        .prog_dev = 									\
						{												\
							.cs_pin = CARIBOULITE_FPGA_SS,				\
							.cdone_pin = CARIBOULITE_FPGA_CDONE,		\
							.reset_pin = CARIBOULITE_FPGA_CRESET,		\
						},												\
                        .initialized = 0,                               \
                    },                                                  \
                    .modem =                                            \
                    {                                                   \
                        .reset_pin = CARIBOULITE_MODEM_RESET,           \
                        .irq_pin = CARIBOULITE_MODEM_IRQ,               \
                        .cs_pin = CARIBOULITE_MODEM_SS,                 \
                        .spi_dev = CARIBOULITE_SPI_DEV,                 \
                        .spi_channel = CARIBOULITE_MODEM_SPI_CHANNEL,   \
                        .initialized = 0,                               \
                        .override_cal = true,                           \
                    },                                                  \
                    .mixer =                                            \
                    {                                                   \
                        .cs_pin = CARIBOULITE_MIXER_SS,                 \
                        .reset_pin = CARIBOULITE_MIXER_RESET,           \
                        .ref_freq_hz = 32e6,                            \
                        .initialized = 0,                               \
                    },                                                  \
                    .reset_fpga_on_startup = 1,                         \
					.system_status = sys_status_unintialized,			\
                }

#ifdef __cplusplus
}
#endif

#endif //__CARIBOULITE_CONFIG_DEFAULT_H__