#ifndef __HERMONSDR_CONFIG_DEFAULT_H__
#define __HERMONSDR_CONFIG_DEFAULT_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "config/config.h"

// PINOUT SPI
#define HERMONSDR_SPI_DEV 1
#define HERMONSDR_MOSI 20
#define HERMONSDR_SCK 21
#define HERMONSDR_MISO 19

// PINOUT FPGA - ICE40
#define HERMONSDR_FPGA_SPI_CHANNEL 2
#define HERMONSDR_FPGA_SS 16
#define HERMONSDR_FPGA_CDONE 25
#define HERMONSDR_FPGA_CS_FLASH 24
#define HERMONSDR_FPGA_CRESET 23

// PINOUT AT86 - AT86RF215
#define HERMONSDR_MODEM_SPI_CHANNEL 0
#define HERMONSDR_MODEM_SS 18
#define HERMONSDR_MODEM_IRQ 22
#define HERMONSDR_MODEM_RESET 23

//=======================================================================================
// SYSTEM DEFINITIONS & CONFIGURATIONS
//=======================================================================================
#define HERMONSDR_CONFIG_DEFAULT(a)                                  	\
                sys_st(a)={                                     		\
                    .board_info = {0},                                  \
                    .spi_dev =                                          \
                    {                                                   \
                        .miso = HERMONSDR_MISO,                       	\
                        .mosi = HERMONSDR_MOSI,                       	\
                        .sck = HERMONSDR_SCK,                         	\
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
                        .reset_pin = HERMONSDR_FPGA_CRESET,          	\
                        .irq_pin = HERMONSDR_MODEM_IRQ,					\
						.cs_pin = HERMONSDR_FPGA_SS,                  	\
                        .spi_dev = HERMONSDR_SPI_DEV,                 	\
                        .spi_channel = HERMONSDR_FPGA_SPI_CHANNEL,    	\
						.prog_dev = 									\
						{												\
							.cs_pin = HERMONSDR_FPGA_SS,				\
							.rreq_cs_flash = HERMONSDR_FPGA_CS_FLASH,	\
							.wreq_cdone_pin = HERMONSDR_FPGA_CDONE,		\
							.reset_pin = HERMONSDR_FPGA_CRESET,			\
						},												\
                        .initialized = 0,                               \
                    },                                                  \
                    .modem =                                            \
                    {                                                   \
                        .reset_pin = HERMONSDR_MODEM_RESET,           	\
                        .irq_pin = HERMONSDR_MODEM_IRQ,               	\
                        .cs_pin = HERMONSDR_MODEM_SS,                 	\
                        .spi_dev = HERMONSDR_SPI_DEV,                 	\
                        .spi_channel = HERMONSDR_MODEM_SPI_CHANNEL,   	\
                        .initialized = 0,                               \
                        .override_cal = true,                           \
                    },                                                  \
                    .reset_fpga_on_startup = 1,                         \
					.force_fpga_reprogramming = 1,						\
					.system_status = sys_status_unintialized,			\
                }

#ifdef __cplusplus
}
#endif

#endif //__HERMONSDR_CONFIG_DEFAULT_H__