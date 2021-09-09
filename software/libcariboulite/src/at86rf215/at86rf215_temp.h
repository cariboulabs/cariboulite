#ifndef _AT86RF215_H_
#define _AT86RF215_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

#include "at86rf215_regs.h"

typedef struct {
    void ( *TxDone )( void* context );
    void ( *TxTimeout )( void* context );
    void ( *RxDone )( uint8_t *payload, uint16_t size, int16_t rssi, int8_t snr, void* context );
    void ( *RxTimeout )( void* context );
    void ( *RxError )( void* context );
    void ( *FhssChangeChannel )( uint8_t currentChannel, void* context );
    void ( *CadDone ) ( bool channelActivityDetected, void* context );
} RadioEvents_t;

/**########################Variables and Types############################**/
/*! Radio driver supported modems */
typedef enum
{
    MODEM_09 = 0,
	MODEM_BR_TX,
	MODEM_BR_RX,
	MODEM_24,
} at86rf215_radio_modems_st;

/* Radio driver internal state machine states definition */
typedef enum {
    RF_TRXOFF = 0,
    RF_TXPREP,
    RF_TX,
    RF_RX,
    RF_TRANSITION,
    RF_RESET,
} at86rf215_radio_state_st;

/* Hardware IO IRQ callback function definition (accepts context) */
typedef void ( at86rf215_irq_handler )( void* );

/* AT86RF215 definitions */
#define TCXO_FREQ                                   26000000
#define FREQ_STEP1                                  99.1821289063
#define FREQ_STEP2                                  198.364257813
#define FREQ_STEP3                                  396.728515625

/* Calibration */
#define TRIM_LOOPS                          7       // Number of trim loops
#define NUM_SUFFICIENT_MEASUREMENTS         3       // Number of sufficient measurements
#define NARROW_TRIM_THRESHOLD               2       // Narrow trim threshold values

/* Radio hardware and global parameters */
typedef enum {
    PHY_OFF = 0,
    PHY_FSK,
    PHY_OFDM,
    PHY_OQPSK,
} at86rf215_bbc_phy_st;

typedef struct
{
    bool                            channelComplient;
    bool                            CTX;
    at86rf215_radio_modems_st       modem;
    at86rf215_radio_state_st        State;
    uint32_t                        channel;
    uint8_t                         power;
    uint8_t                         IRQM;				//RF interrupt enables
    uint8_t                         TXSR;				//RF transmitter sampling frequency
    uint8_t                         RXSR;				//RF RECEIVER sampling frequency
    uint8_t                         RXCUTOFF;			//RF RECEIVER CUTOFF frequency
    uint8_t                         RXBW;				//RF RECEIVER BW
    uint8_t                         TXCUTOFF;			//RF TRANSMITTER CUTOFF frequency
} at86rf215_rf_settings_t;

typedef struct
{
    at86rf215_bbc_phy_st            Phy;
    uint8_t                         BBEN_PT;
    uint8_t                         IRQM;		//Baseband interrupt enables
    bool                            directMod;
    bool                            dataWhite;
} at86rf215_bbc_settings_t;

typedef struct AT86RF215_s
{
    // Interfaces
    int reset_pin;
	int irq_pin;
    int spi_dev;
    int spi_channel;
    int spi_baud;
    int spi_mode;
	int gpio_chip_handle;

    RF_Settings_t       RF_Settings;
    BBC_Settings_t      BBC_Settings;
    bool                Continuous;
} at86rf215_st;

typedef struct
{
    int8_t   Power;
//    uint32_t Bandwidth;
//    uint32_t Datarate;
//    bool     LowDatarateOptimize;
//    uint8_t  Coderate;
//    uint16_t PreambleLen;
//    bool     FixLen;
//    uint8_t  PayloadLen;
//    bool     CrcOn;
//    bool     FreqHopOn;
//    uint8_t  HopPeriod;
//    bool     IqInverted;
//    bool     RxContinuous;
//    uint32_t TxTimeout;
} at86rf215_radio_09_settings_st;

struct At86rf215_Radio_s
{
    void    ( *Init )( RadioEvents_t *events );
    at86rf215_radio_state_st ( *GetStatus )( void );
    void    ( *SetModem )( At86rf215_RadioModems_t modem );
    void    ( *SetChannel )( uint32_t freq );
    bool    ( *IsChannelFree )( At86rf215_RadioModems_t modem, uint32_t freq, int16_t rssiThresh );
    uint32_t ( *Random )( void );
    void    ( *SetRxConfig )( At86rf215_RadioModems_t modem, uint32_t bandwidth,
                              uint32_t datarate, uint8_t coderate,
                              uint32_t bandwidthAfc, uint16_t preambleLen,
                              uint16_t symbTimeout, bool fixLen,
                              uint8_t payloadLen,
                              bool crcOn, bool FreqHopOn, uint8_t HopPeriod,
                              bool iqInverted, bool rxContinuous );
    void    ( *SetTxConfig )( At86rf215_RadioModems_t modem, int8_t power, uint32_t fdev,
                              uint32_t bandwidth, uint32_t datarate,
                              uint8_t coderate, uint16_t preambleLen,
                              bool fixLen, bool crcOn, bool FreqHopOn,
                              uint8_t HopPeriod, bool iqInverted, uint32_t timeout );
    bool    ( *CheckRfFrequency )( uint32_t frequency );
    uint32_t  ( *TimeOnAir )( At86rf215_RadioModems_t modem, uint8_t pktLen );
    void    ( *Send )( uint8_t *buffer, uint8_t size );
    void    ( *Sleep )( void );
    void    ( *Standby )( void );
    void    ( *Rx )( uint32_t timeout );
    void    ( *StartCad )( void );
    int16_t ( *Rssi )( At86rf215_RadioModems_t modem );
    void    ( *Write )( uint16_t addr, uint8_t data );
    uint8_t ( *Read )( uint16_t addr );
    void    ( *WriteBuffer )( uint16_t addr, uint8_t *buffer, uint8_t size );
    void    ( *ReadBuffer )( uint16_t addr, uint8_t *buffer, uint8_t size );
    void ( *SetMaxPayloadLength )( At86rf215_RadioModems_t modem, uint8_t max );
};


extern const struct At86rf215_Radio_s At86rf215Radio;


/**########################External Function############################**/
void AT86RF215Init( RadioEvents_t *events );
at86rf215_radio_state_st AT86RF215GetStatus( void );
void AT86RF215SetModem( at86rf215_radio_modems_st modem );
void AT86RF215SetChannel( uint32_t freq );
bool AT86RF215IsChannelFree( at86rf215_radio_modems_st modem, uint32_t freq, int16_t rssiThresh );
uint32_t AT86RF215Random( void );
void AT86RF215RxSetConfig( at86rf215_radio_modems_st modem, uint32_t bandwidth,
                         uint32_t datarate, uint8_t coderate,
                         uint32_t bandwidthAfc, uint16_t preambleLen,
                         uint16_t symbTimeout, bool fixLen,
                         uint8_t payloadLen,
                         bool crcOn, bool FreqHopOn, uint8_t HopPeriod,
                         bool iqInverted, bool rxContinuous );

void AT86RF215TxSetConfig( at86rf215_radio_modems_st modem, int8_t power, uint32_t fdev,
                        uint32_t bandwidth, uint32_t datarate,
                        uint8_t coderate, uint16_t preambleLen,
                        bool fixLen, bool crcOn, bool FreqHopOn,
                        uint8_t HopPeriod, bool iqInverted, uint32_t timeout );

uint32_t AT86RF215GetTimeOnAir( at86rf215_radio_modems_st modem, uint8_t pktLen );
void AT86RF215Send( uint8_t *buffer, uint8_t size );
void AT86RF215SetSleep( void );
void AT86RF215SetStby( void );
void AT86RF215RxSet( uint32_t timeout );
void AT86RF215StartCad( void );
int16_t AT86RF215ReadRssi( at86rf215_radio_modems_st modem );
void AT86RF215Write( uint16_t addr, uint8_t data );
uint8_t AT86RF215Read( uint16_t addr );
void AT86RF215WriteBuffer(uint16_t addr, uint8_t *buffer, uint8_t size);
void AT86RF215ReadBuffer(uint16_t addr, uint8_t *buffer, uint8_t size);
void AT86RF215WriteFifo( uint8_t *buffer, uint8_t size );
void AT86RF215SetMaxPayloadLength( at86rf215_radio_modems_st modem, uint8_t max );
void AT86RF215Reset( void );
void AT86RF215OnDio0Irq(void);
void AT86RF215SetOpMode( uint8_t mode );
void AT86RF215SetInfMode( uint8_t mode );
void AT86RF215SetPHYType(uint8_t BBEN_PT);
void AT86RF215SetRFIntr(uint8_t INTR_SET);
void AT86RF215SetBBIntr(uint8_t INTR_SET);
uint8_t AT86RF215GetState(void);
void AT86RF215SetState(uint8_t state);
void AT86RF215SetRFMode(uint8_t mode);
void AT86RF215SetIQRX(void);
void AT86RF215IRQInit(void);
void AT86RF215RxSetBW(uint8_t BW);
void AT86RF215RxSetSR(uint8_t RXSR);
void AT86RF215RxSetCutOff(uint8_t RXCUTOFF);
void AT86RF215RxSetIQ(uint32_t freq);
void AT86RF215SetCWSingleTone(uint32_t freq);
void AT86RF215Set09CWSingleToneTest(void);
void AT86RF215TxSetPwr(uint8_t PWR);
void AT86RF215TxSetPAC(uint8_t PAC);
void AT86RF215TxSetFrameLength(uint16_t FrameLen);
void AT86RF215TxSetContinuous(bool CTX);
void AT86RF215TxSetSR(uint8_t TXSR);
void AT86RF215TxSetDirectMod(bool DM);
void AT86RF215TxSetDataWhite(bool DW);
void AT86RF215TxSetCutOff(uint8_t TXCUTOFF);
void AT86RF215TxSetIQ(uint32_t freq);
void AT86RF215TxSet( uint32_t timeout );
void AT86RF215SetRxChannel(uint32_t freq);
void AT86RF215RxSetIFS(uint8_t IFS);
void AT86RF215SetTXCI(uint8_t txci);
void AT86RF215SetTXCQ(uint8_t txcq);
void AT86RF215SetCLKO(uint8_t clko);
void AT86RF215TxSetPAVC(uint8_t PAVC);
void AT86RF215RxSetBBFSK(uint32_t freq);
void AT86RF215Initialize(uint32_t freq);
void AT86RF215SetTXBBFSK(uint32_t freq);
void AT86RF215SetXOCTrim(uint8_t trim);

#ifdef __cplusplus
}
#endif

#endif
