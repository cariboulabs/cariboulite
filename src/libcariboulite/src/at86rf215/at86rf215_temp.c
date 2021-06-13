#include "at86rf215.h"
#include <math.h>
#include <string.h>


/**########################Variables and Types############################**/
static RadioEvents_t *RadioEvents;
AT86RF215_t AT86RF215;

/**########################Internal functions############################**/
void bitSet(uint8_t *value, uint8_t bit);
void bitClear(uint8_t *value, uint8_t bit);
void bitWrite(uint16_t addr, uint8_t pos, uint8_t newValue);
uint8_t bitRead(uint16_t addr, uint8_t pos);
void AT86RF215ReadFifo( uint8_t *buffer, uint8_t size );
void AT86RF215SetOpMode( uint8_t opMode );

void AT86RF215SetLVDSCMV(bool v1_2, uint8_t cmv);
void AT86RF215SetIRQMask(bool status, uint8_t pos);
void AT86RF215SetIQSkewDrive(uint8_t skew);
void AT86RF215SetIQCurrentDrive(uint8_t drive);
void AT86RF215AGCSetTGT(uint8_t tgt);
void AT86RF215AGCSetAGCC(bool agci, uint8_t agc_average);

/**########################External functions############################**/
void AT86RF215Init(RadioEvents_t *events)
{
    RadioEvents = events;
    AT86RF215Reset();
    AT86RF215.RF_Settings.channelComplient = false;
}


At86rf215_RadioState_t AT86RF215GetStatus(void)
{
	//TODO:
	At86rf215_RadioState_t test = RF_TRXOFF;
    return test;
}

void AT86RF215SetModem(At86rf215_RadioModems_t modem)
{
    AT86RF215.RF_Settings.Modem = modem;
}

void AT86RF215RxSetIFS(uint8_t IFS)
{
	if (AT86RF215.RF_Settings.Modem == MODEM_09){
		bitWrite(REG_RF09_RXBWC, 4, IFS);
	} else if (AT86RF215.RF_Settings.Modem == MODEM_24){
		bitWrite(REG_RF24_RXBWC, 4, IFS);
	}
	else{
		PrintError(ERROR_Modem);
	}
}

void AT86RF215SetTXCI(uint8_t txci)
{
	txci &= 0x3F;
	AT86RF215Write(REG_RF09_TXCI, txci);
}

void AT86RF215SetTXCQ(uint8_t txcq)
{
	txcq &= 0x3F;
	AT86RF215Write(REG_RF09_TXCQ, txcq);
}


void AT86RF215SetChannel( uint32_t freq )
{
//    uint8_t RFn_CS;
    uint8_t RFn_CCF0H;
    uint8_t RFn_CCF0L;
    uint8_t RFn_CNL;
    uint8_t RFn_CNM;
    double temp;

    AT86RF215.RF_Settings.Channel = freq;

	if ((freq <= 510000000) && (freq >= 389500000))
	{
		freq = freq - 377000000;

		freq =      (uint32_t) ((double)freq / (double)FREQ_STEP1);
		RFn_CCF0H =   (uint8_t)  ((freq >> 16) & 0xFF);
		RFn_CCF0L = (uint8_t)  ((freq >> 8) & 0xFF);
		RFn_CNL =   (uint8_t)  (freq & 0xFF);
		RFn_CNM = 0x40;
	}
	else if ((freq >= 779000000) && (freq <= 1020000000))
	{
		freq = freq - 754000000;
		temp =  (double) (freq);
		temp = (temp * 65536) / 13000000;
		freq = (uint32_t) temp;

//		freq =      (uint32_t) ((double)freq / (double)FREQ_STEP2);
//		freq =      (uint32_t) (((long)freq * (long)(65536)) / long(13000000));

		RFn_CCF0H =   (uint8_t)  ((freq >> 16) & 0xFF);
//        printf("reg value: %x \n", RFn_CCF0H);

		RFn_CCF0L = (uint8_t)  ((freq >> 8) & 0xFF);//25KHz off, so we added 25KHz to compensate for that
//		printf("reg value: %x \n", RFn_CCF0L);

		RFn_CNL =   (uint8_t)  (freq & 0xFF);
//		printf("reg value: %x \n", RFn_CNL);
		RFn_CNM = 0x80;

		/* set REG_RFn_CCF0L */
		AT86RF215Write(REG_RF09_CCF0L, RFn_CCF0L);

		/* set REG_RFn_CCF0H */
		AT86RF215Write(REG_RF09_CCF0H, RFn_CCF0H);

		/* set REG_RFn_CNL */
		AT86RF215Write(REG_RF09_CNL, RFn_CNL);

		/* set REG_RFn_CNM */
		AT86RF215Write(REG_RF09_CNM, RFn_CNM);

	}
	else if ((freq >= 2400000000) && (freq <= 2483500000))
	{
		freq = freq - 2366000000;
		freq =      (uint32_t) ((double)freq / (double)FREQ_STEP3);
		RFn_CCF0H =   (uint8_t)  ((freq >> 16) & 0xFF);
		RFn_CCF0L = (uint8_t)  ((freq >> 8) & 0xFF);
		RFn_CNL =   (uint8_t)  (freq & 0xFF);
		RFn_CNM = 0xC0;

		AT86RF215Write(REG_RF24_CCF0L, RFn_CCF0L);
		AT86RF215Write(REG_RF24_CCF0H, RFn_CCF0H);
		AT86RF215Write(REG_RF24_CNL, RFn_CNL);
		AT86RF215Write(REG_RF24_CNM, RFn_CNM);
	}
	else
	{
//            TODO
		PrintError(ERROR_Frequency);
	}

}

bool AT86RF215IsChannelFree( At86rf215_RadioModems_t modem, uint32_t freq, int16_t rssiThresh )
{
	//TODO
    return 0;
}

uint32_t AT86RF215Random( void )
{
	//TODO
	return  0;
}

void AT86RF215RxSetConfig( At86rf215_RadioModems_t modem, uint32_t bandwidth,
                         uint32_t datarate, uint8_t coderate,
                         uint32_t bandwidthAfc, uint16_t preambleLen,
                         uint16_t symbTimeout, bool fixLen,
                         uint8_t payloadLen,
                         bool crcOn, bool FreqHopOn, uint8_t HopPeriod,
                         bool iqInverted, bool rxContinuous )
{
	//TODO
}

void AT86RF215TxSetConfig( At86rf215_RadioModems_t modem, int8_t power, uint32_t fdev,
                        uint32_t bandwidth, uint32_t datarate,
                        uint8_t coderate, uint16_t preambleLen,
                        bool fixLen, bool crcOn, bool FreqHopOn,
                        uint8_t HopPeriod, bool iqInverted, uint32_t timeout )
{
	//TODO
}

uint32_t AT86RF215GetTimeOnAir( At86rf215_RadioModems_t modem, uint8_t pktLen )
{
	//TODO
    return 0;
}

void AT86RF215Send( uint8_t *buffer, uint8_t size )
{
	//TODO
}

void AT86RF215SetSleep( void )
{
	//TODO
}

void AT86RF215SetStby( void )
{
	//TODO
}

void AT86RF215RxSet( uint32_t timeout )
{
	//TODO
}

void AT86RF215StartCad( void )
{
	//TODO
}

int16_t AT86RF215ReadRssi( At86rf215_RadioModems_t modem )
{
	//TODO
	return 0;
}

void AT86RF215Write( uint16_t addr, uint8_t data )
{
    AT86RF215WriteBuffer( addr, &data, 1 );
}

uint8_t AT86RF215Read( uint16_t addr )
{
    uint8_t data;
    /* SPI reads previous byte */
    AT86RF215ReadBuffer(addr, &data, 1);
    return data;
}


void AT86RF215WriteBuffer( uint16_t addr, uint8_t *buffer, uint8_t size )
{
    uint8_t i;

    uint8_t addr0 = ((addr >> 8) & 0x3F) | 0x80;
    uint8_t addr1 = addr & 0xFF;

    GpioWrite( &AT86RF215.Spi.NSS, 0 );

    SpiInOut_IQRadio(&AT86RF215.Spi, addr0);
    SpiInOut_IQRadio(&AT86RF215.Spi, addr1);

    for( i = 0; i < size; i++ )
    {
    	SpiInOut_IQRadio( &AT86RF215.Spi, buffer[i]);
    }

    GpioWrite( &AT86RF215.Spi.NSS, 1 );
}

void AT86RF215ReadBuffer(uint16_t addr, uint8_t *buffer, uint8_t size)
{
    uint8_t i;
    uint8_t addr0 = (addr >> 8) & 0x3F;
    uint8_t addr1 = addr & 0xFF;

    GpioWrite(&AT86RF215.Spi.NSS, 0);

    SpiInOut_IQRadio(&AT86RF215.Spi, addr0);
    SpiInOut_IQRadio(&AT86RF215.Spi, addr1);

    for( i = 0; i < size; i++ )
    {
        buffer[i] = SpiInOut_IQRadio(&AT86RF215.Spi, 0);
    }

    GpioWrite(&AT86RF215.Spi.NSS, 1);
}

void AT86RF215SetMaxPayloadLength( At86rf215_RadioModems_t modem, uint8_t max )
{
	//TODO
}

void AT86RF215Reset( void )
{
    /* Ensure control lines have correct levels */
    GpioWrite(&AT86RF215.Reset, true);
    /* Wait typical time of timer TR1. */
    delay_us(300);
    /* Set RESET pin to 0 */
    GpioWrite(&AT86RF215.Reset, false);
    /* Wait 10 us */
    delay_us(300);
    GpioWrite(&AT86RF215.Reset, true);
}

void AT86RF215WriteFifo( uint8_t *buffer, uint8_t size )
{
    AT86RF215WriteBuffer( 0, buffer, size );
}

void AT86RF215ReadFifo( uint8_t *buffer, uint8_t size )
{
    AT86RF215ReadBuffer( 0, buffer, size );
}

void AT86RF215SetInfMode( uint8_t mode )
{
    mode = mode & 0x07;
    uint8_t temp = AT86RF215Read(REG_RF_IQIFC1);
    uint8_t data;

    /* set FAILSF bit */
    data = temp >> 7;

    /* set CHPM bits */
    data = data << 3;
    data = data | mode;

    /* rewrite bit 3 to 0 */
    data = data << 4;
    int i=0;
    for (i=0; i<4; i++)
    {
        if (((temp >> i) & 0x01) == 0x01)
            bitSet(&data, i);
        /* otherwise is already 0 */
    }
    AT86RF215Write(REG_RF_IQIFC1, data);
}

uint8_t AT86RF215GetState(void)
{
    uint8_t current_state;
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        current_state = AT86RF215Read(REG_RF09_STATE);
        current_state &= 0x07;
    } else if (AT86RF215.RF_Settings.Modem == MODEM_24){
        current_state = AT86RF215Read(REG_RF24_STATE);
        current_state &= 0x07;
    } else {
    	PrintError(ERROR_Modem);
    	return 0;
    }
    return current_state;
}


void AT86RF215SetXOCTrim(uint8_t trim)
{
	trim &= 0x0F;
	uint8_t current_reg = AT86RF215Read(REG_RF_XOC);
	current_reg &= 0xF0;
	current_reg += trim;
	AT86RF215Write(REG_RF_XOC, current_reg);
}

void AT86RF215SetState(uint8_t state)
{
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        AT86RF215Write(REG_RF09_CMD, state & 0x07);
    } else if (AT86RF215.RF_Settings.Modem == MODEM_24){
        AT86RF215Write(REG_RF24_CMD, state & 0x07);
    } else {
    	PrintError(ERROR_Modem);
    }
}

void AT86RF215SetRFMode(uint8_t CHPM)
{
    CHPM = CHPM << 4;
    uint8_t current_reg = AT86RF215Read(REG_RF_IQIFC1);
    current_reg &= 0x8F;
    current_reg += CHPM;
    AT86RF215Write(REG_RF_IQIFC1, current_reg);
}

void AT86RF215Set09CWSingleToneTest(void)
{
    /* Check which state, we should go to TRXOFF to set the registers */
    uint8_t current_state=AT86RF215Read(REG_RF09_STATE);
    if (current_state != RF_STATE_TRXOFF){
        AT86RF215Write(REG_RF09_CMD, RF_STATE_TRXOFF);
    }
    current_state=AT86RF215Read(REG_RF09_STATE);
//    printf("current state: %x \n", current_state);


    AT86RF215TxSetPwr(0x05);
    AT86RF215TxSetContinuous(0x01);
    AT86RF215SetPHYType(BB_PHY_FSK);
    AT86RF215TxSetFrameLength(0x0001);

    /* set the frame equal to 0 */
    AT86RF215Write(REG_BBC0_FBTXS, 0x00);
    AT86RF215TxSetSR(0x0A);
    AT86RF215TxSetDirectMod(true);
//    AT86RF215Write(REG_BBC0_FSKDM, 0x01); //Set FSK direct modulation
//    bitWrite(REG_RF09_TXDFE, 4, 1);

    /* Make sure it does not do data whitening */
    AT86RF215TxSetDataWhite(0x00);
    AT86RF215SetChannel(903000000);
    /* Go to TXPREP => check for TRXRDY interrupt!! */
    AT86RF215Write(REG_RF09_CMD, RF_STATE_TXPREP);

//    current_state=AT86RF215Read(REG_RF09_STATE);
//    printf("current state: %x \n", current_state);

    /* Go to TX */
    AT86RF215Write(REG_RF09_CMD, RF_STATE_TX);

    current_state=AT86RF215Read(REG_RF09_STATE);
//    printf("current state: %x \n", current_state);
}

void AT86RF215SetCWSingleTone(uint32_t freq)
{
    uint8_t current_state = AT86RF215GetState();
    if (current_state != RF_STATE_TRXOFF){
        AT86RF215SetState(RF_STATE_TRXOFF);
    }
    /* set PA current reduction */
    AT86RF215TxSetPAC(RF_PAC_0dB_Reduction);

    /* PA DC voltage */
    AT86RF215TxSetPAVC(RF_PA_VC_2_0);

    /* set PA power */
    AT86RF215TxSetPwr(0x1F);

    AT86RF215TxSetContinuous(true);

    AT86RF215SetPHYType(BB_PHY_FSK);


    AT86RF215TxSetFrameLength(0x0001);

    /* set the frame equal to 0 */
    AT86RF215Write(REG_BBC1_FBTXS, 0x00);

    AT86RF215TxSetSR(RF_SR4000);

    AT86RF215TxSetDirectMod(true);

    /* Make sure it does not do data whitening */
    AT86RF215TxSetDataWhite(false);

    /* set synthesizer frequency */
    AT86RF215SetChannel(freq);

    /* set cut-off frequency */
    AT86RF215TxSetCutOff(RF_CUT_4_4);

    uint8_t PAC = AT86RF215Read(REG_RF09_PAC);
//    printf("PAC: %x\n", PAC);

    uint8_t AUXS = AT86RF215Read(REG_RF09_AUXS);
//    printf("AUXS: %x\n", AUXS);

    /* Go to TXPREP => check for TRXRDY interrupt!! */
    //TODO
    AT86RF215SetState(RF_STATE_TXPREP);
}


void AT86RF215Initialize(uint32_t freq)
{
/* TX settings */
	/* set PA current reduction */
	AT86RF215TxSetPAC(RF_PAC_3dB_Reduction);
	/* PA DC voltage */
	AT86RF215TxSetPAVC(RF_PA_VC_2_4);
	/* set PA power */
	AT86RF215TxSetPwr(0x1f);
//	AT86RF215TxSetContinuous(true);
//	AT86RF215TxSetFrameLength(0x0001);
	/* set the frame equal to 0 */
//	AT86RF215Write(REG_BBC1_FBTXS, 0x00);
//	AT86RF215TxSetDirectMod(true);
	/* Make sure it does not do data whitening */
//	AT86RF215TxSetDataWhite(false);
	/* set cut-off frequency */
	AT86RF215TxSetCutOff(RF_CUT_4_4);
//	AT86RF215SetPHYType(BB_PHY_FSK);

/* RX settings */
	/* set bandwidth */
	AT86RF215RxSetBW(RF_BW2000KHZ_IF2000KHZ);
	/* set IFS */
	AT86RF215RxSetIFS(RX_IFS_Deactive);
	/* set IQ serial Skew drive */
	AT86RF215SetIQSkewDrive(RF_IQ_SKEW_zero);
	/* set IQ common voltage */
	AT86RF215SetLVDSCMV(false, RF_IQ_LVDS_CMV200);
//    AT86RF215SetLVDSCMV(false, RF_IQ_LVDS_CMV150);
	/* set IQ current drive */
	AT86RF215SetIQCurrentDrive(RF_IQ_DRV_Current_2mA);
	/* disable CLKO */
	AT86RF215SetCLKO(RF_CLKO_OFF);
	/* AGC */
//	AT86RF215AGCSetGCW(0x17);
	AT86RF215AGCSetAGCC(false, RF_AGC_AVGS_8);
    AT86RF215AGCSetTGT(0x00);
	/* set cut-off frequency */
	AT86RF215RxSetCutOff(RF_CUT_1_4);

/* Set common settings */
	/* set IRQ Mask */
	AT86RF215SetIRQMask(false, RF_IRQM_TRXRDY);
	AT86RF215SetIRQMask(false, RF_IRQM_WAKEUP);
	/* set sampling rate */
	AT86RF215RxSetSR(RF_SR4000);
	/* set frequency */
	AT86RF215SetChannel(freq);

	/* set RF_IQIFC1 RF mode */
	AT86RF215SetRFMode(RF_MODE_RF);
}


void AT86RF215SetTXBBFSK(uint32_t freq)
{
    /* set PA current reduction */
    AT86RF215TxSetPAC(RF_PAC_0dB_Reduction);

    /* PA DC voltage */
    AT86RF215TxSetPAVC(RF_PA_VC_2_0);

    /* set PA power */
    AT86RF215TxSetPwr(0x10);

    AT86RF215TxSetContinuous(true);

    AT86RF215SetPHYType(BB_PHY_FSK);


    AT86RF215TxSetFrameLength(0x0001);

    /* set the frame equal to 0 */
    AT86RF215Write(REG_BBC1_FBTXS, 0x00);

    AT86RF215TxSetSR(RF_SR4000);

    AT86RF215TxSetDirectMod(true);

    /* Make sure it does not do data whitening */
    AT86RF215TxSetDataWhite(false);

    /* set synthesizer frequency */
    AT86RF215SetChannel(freq);

    /* set cut-off frequency */
    AT86RF215TxSetCutOff(RF_CUT_4_4);


    uint8_t PAC = AT86RF215Read(REG_RF09_PAC);
//    printf("PAC: %x\n", PAC);

    uint8_t AUXS = AT86RF215Read(REG_RF09_AUXS);
//    printf("AUXS: %x\n", AUXS);

    /* Go to TXPREP => check for TRXRDY interrupt!! */
    //TODO
    AT86RF215SetState(RF_STATE_TXPREP);
}

void AT86RF215RxSetBBFSK(uint32_t freq)
{
    /* Check which state, we should go to TRXOFF to set the registers */
    uint8_t current_state=AT86RF215Read(REG_RF09_STATE);
    if (current_state != RF_STATE_TRXOFF){
        AT86RF215Write(REG_RF09_CMD, RF_STATE_TRXOFF);
    }
    current_state=AT86RF215Read(REG_RF09_STATE);
//    printf("current state: %x \n", current_state);

    AT86RF215SetBBIntr(BB_INTR_RXFS + BB_INTR_RXFE);
    AT86RF215SetPHYType(BB_PHY_FSK);

    AT86RF215SetRFMode(RF_MODE_BBRF09);

    AT86RF215SetChannel(freq);

    AT86RF215Write(REG_RF09_CMD, RF_STATE_TXPREP);

    /* Go to RX */
//    AT86RF215Write(REG_RF09_CMD, RF_STATE_RX);

    current_state=AT86RF215Read(REG_RF09_STATE);
//    printf("current state: %x \n", current_state);

//    while(1){
//        if (MCU_State == MCU_STATE_INTR){
//            printf("MAIN STATE : INTR\n");
//
//            uint8_t RF09_IRQS_REG_RD = AT86RF215Read(REG_RF09_IRQS);
//            uint8_t BBC0_IRQS_REG_RD = AT86RF215Read(REG_BBC0_IRQS);
//
//            if((BBC0_IRQS_REG_RD & BB_INTR_RXFS) == BB_INTR_RXFS){
//                printf("BB_INTR_RXFS\n");
//            }
//            if((BBC0_IRQS_REG_RD & BB_INTR_RXFE) == BB_INTR_RXFE){
//                printf("BB_INTR_RXFE\n");
//            }
//            printf("BBC0_IRQS_REG_RD : %x \n", BBC0_IRQS_REG_RD);
//        }
//    }
    // When in RX: -read frame length    - check for valid phy header interrupt
    // - check for successful frame reception interrupt     -
}


void AT86RF215SetRFIntr(uint8_t INTR_SET)
{
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        if (AT86RF215.RF_Settings.IRQM != INTR_SET)
            AT86RF215Write(REG_RF09_IRQM, INTR_SET);
    } else{
        if (AT86RF215.RF_Settings.IRQM != INTR_SET)
            AT86RF215Write(REG_RF24_IRQM, INTR_SET);
    }
    AT86RF215.RF_Settings.IRQM = INTR_SET;
}

void AT86RF215SetBBIntr(uint8_t INTR_SET)
{
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        if (AT86RF215.BBC_Settings.IRQM != INTR_SET){
            AT86RF215Write(REG_BBC0_IRQM, INTR_SET);
        }
    } else{
        if (AT86RF215.BBC_Settings.IRQM != INTR_SET){
            AT86RF215Write(REG_BBC1_IRQM, INTR_SET);
        }
    }
    AT86RF215.BBC_Settings.IRQM = INTR_SET;
}

void AT86RF215SetPHYType(uint8_t BBEN_PT)
{
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        if (AT86RF215.BBC_Settings.BBEN_PT != BBEN_PT){
        	/* mask the PHY Type */
            BBEN_PT &= 0x07;
            uint8_t current_reg = AT86RF215Read(REG_BBC0_PC);
            current_reg &= 0xF8;
            current_reg += BBEN_PT;
            AT86RF215Write(REG_BBC0_PC, current_reg);
        }
    } else{
        if (AT86RF215.BBC_Settings.BBEN_PT != BBEN_PT){
        	/* mask the PHY Type */
            BBEN_PT &= 0x07;
            uint8_t current_reg = AT86RF215Read(REG_BBC1_PC);
            current_reg &= 0xF8;
            current_reg += BBEN_PT;
            AT86RF215Write(REG_BBC1_PC, current_reg);
        }
    }
//    AT86RF215.BBC_Settings.Phy = BBEN_PT;
    AT86RF215.BBC_Settings.BBEN_PT = BBEN_PT;
}


void AT86RF215RxSetBW(uint8_t RXBW)
{
    RXBW &= 0x0F;
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        if (AT86RF215.RF_Settings.RXBW != RXBW){
            uint8_t current_reg = AT86RF215Read(REG_RF09_RXBWC);
            current_reg &= 0xF0;
            current_reg += RXBW;
            AT86RF215Write(REG_RF09_RXBWC, current_reg);
        }
    }else if (AT86RF215.RF_Settings.Modem == MODEM_24){
        if (AT86RF215.RF_Settings.RXBW != RXBW){
            uint8_t current_reg = AT86RF215Read(REG_RF24_RXBWC);
            current_reg &= 0xF0;
            current_reg += RXBW;
            AT86RF215Write(REG_RF24_RXBWC, current_reg);
        }
    }
    else
    {
    	PrintError(ERROR_Modem);
    }
    AT86RF215.RF_Settings.RXBW = RXBW;
}

void AT86RF215SetCLKO(uint8_t clko)
{
	clko &= 0x07;
    uint8_t current_reg = AT86RF215Read(REG_RF_CLKO);
	current_reg &= 0xF8;
	current_reg += clko;
	AT86RF215Write(REG_RF_CLKO, current_reg);
}

void AT86RF215RxSetSR(uint8_t RXSR)
{
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        if (AT86RF215.RF_Settings.RXSR != RXSR){
        	/* mask the SR */
        	RXSR &= 0x0F;
            uint8_t current_reg = AT86RF215Read(REG_RF09_RXDFE);
            current_reg &= 0xF0;
            current_reg += RXSR;
            AT86RF215Write(REG_RF09_RXDFE, current_reg);
        }
    } else if (AT86RF215.RF_Settings.Modem == MODEM_24){
        if (AT86RF215.RF_Settings.RXSR != RXSR){
        	/* mask the SR */
        	RXSR &= 0x0F;
            uint8_t current_reg = AT86RF215Read(REG_RF24_RXDFE);
            current_reg &= 0xF0;
            current_reg += RXSR;
            AT86RF215Write(REG_RF24_RXDFE, current_reg);
        }
    } else{
    	PrintError(ERROR_Modem);
    	return;
    }
    AT86RF215.RF_Settings.RXSR = RXSR;
}


void AT86RF215RxSetCutOff(uint8_t RXCUTOFF)
{
    RXCUTOFF &= 0x07;
    RXCUTOFF = RXCUTOFF << 5;
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        if (AT86RF215.RF_Settings.RXCUTOFF != RXCUTOFF){
            uint8_t current_reg = AT86RF215Read(REG_RF09_RXDFE);
            current_reg &= 0x1F;
            current_reg += RXCUTOFF;
            AT86RF215Write(REG_RF09_RXDFE, current_reg);
        }
    } else if (AT86RF215.RF_Settings.Modem == MODEM_24){
        if (AT86RF215.RF_Settings.RXCUTOFF != RXCUTOFF){
            uint8_t current_reg = AT86RF215Read(REG_RF24_RXDFE);
            current_reg &= 0x1F;
            current_reg += RXCUTOFF;
            AT86RF215Write(REG_RF24_RXDFE, current_reg);
        }
    } else{
    	PrintError(ERROR_Modem);
    	return;
    }
    AT86RF215.RF_Settings.RXCUTOFF = RXCUTOFF;
}


void AT86RF215RxSetIQ(uint32_t freq)
{
    uint8_t current_state = AT86RF215GetState();
//    printf("current_state: %x\n", current_state);

    if (current_state != RF_STATE_TRXOFF)
        AT86RF215SetState(RF_CMD_TRXOFF);

    /* set RF_IQIFC1 RF mode */
    AT86RF215SetRFMode(RF_MODE_RF);

    /* set bandwidth */
    AT86RF215RxSetBW(RF_BW160KHZ_IF250KHZ);

    /* set IFS */
    AT86RF215RxSetIFS(RX_IFS_Deactive);

    /* set IQ serial Skew drive */
    AT86RF215SetIQSkewDrive(RF_IQ_SKEW_zero);

    /* set IQ common voltage */
//    AT86RF215SetLVDSCMV(true, RF_IQ_LVDS_CMV200);
    AT86RF215SetLVDSCMV(false, RF_IQ_LVDS_CMV200);
//    AT86RF215SetLVDSCMV(false, RF_IQ_LVDS_CMV150);

    /* set IQ current drive */
    AT86RF215SetIQCurrentDrive(RF_IQ_DRV_Current_2mA);

    /* disable CLKO */
    AT86RF215SetCLKO(RF_CLKO_OFF);

    uint8_t tec = AT86RF215Read(REG_RF_IQIFC0);
//	printf("IQIFC0 Final: %x \n", tec);

	/* set frequency */
    AT86RF215SetChannel(freq);

    /* AGC */
//    AT86RF215AGCSetGCW(0x17);
    AT86RF215AGCSetAGCC(false, RF_AGC_AVGS_8);
    AT86RF215AGCSetTGT(0x00);


    uint8_t cur_reg = AT86RF215Read(REG_RF09_AGCC);
//    printf("AGCC: %x\n", cur_reg);

    /* set sampling rate */
    AT86RF215RxSetSR(RF_SR4000);

    /* set cut-off frequency */
    AT86RF215RxSetCutOff(RF_CUT_1_4);

    uint8_t st = AT86RF215GetState();
//    printf("current_state: %x\n", st);

    uint8_t agcc = AT86RF215Read(REG_RF09_AGCC);
    uint8_t agcs = AT86RF215Read(REG_RF09_AGCS);
//    printf("agcc: %x\n", agcc);
//    printf("agcs: %x\n", agcs);

    /* set IRQ Mask */
    AT86RF215SetIRQMask(true, RF_IRQM_TRXRDY);
    AT86RF215SetIRQMask(false, RF_IRQM_WAKEUP);

    uint8_t mask = AT86RF215Read(REG_RF09_IRQM);
//    printf("mask: %x\n", mask);

    /* set receive mode */
//    AT86RF215SetState(RF_CMD_RX);
    AT86RF215SetState(RF_CMD_TXPREP);
}

void AT86RF215IRQInit(void)
{
	uint8_t current_state=AT86RF215Read(REG_RF_CFG);
//	printf("current state: %x \n", current_state);

	uint8_t RF_IRQS_REG = AT86RF215Read(REG_RF09_IRQS);
//	printf("RF09 IRQ Reg : %x \n", RF_IRQS_REG);

	RF_IRQS_REG = AT86RF215Read(REG_RF24_IRQS);
//	printf("RF24 IRQ Reg : %x \n", RF_IRQS_REG);

	RF_IRQS_REG = AT86RF215Read(REG_RF09_IRQS);
//	printf("RF09 IRQ Reg : %x \n", RF_IRQS_REG);

	RF_IRQS_REG = AT86RF215Read(REG_RF24_IRQS);
//	printf("RF24 IRQ Reg : %x \n", RF_IRQS_REG);
}

void AT86RF215TxSetFrameLength(uint16_t FrameLen)
{
    /* Setting the frame length. MSB reg is just 3bits */
    uint8_t FrameLenH = ((FrameLen >> 8) & 0x07);
    uint8_t FrameLenL = (FrameLen & 0xFF);

    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        AT86RF215Write(REG_BBC0_TXFLH, FrameLenH);
        AT86RF215Write(REG_BBC0_TXFLL, FrameLenL);
        }
    else{
        AT86RF215Write(REG_BBC1_TXFLH, FrameLenH);
        AT86RF215Write(REG_BBC1_TXFLL, FrameLenL);
    }
}

void AT86RF215TxSetPAVC(uint8_t PAVC)
{
	PAVC &= 0x03;
	if (AT86RF215.RF_Settings.Modem == MODEM_09){
		uint8_t current_reg = AT86RF215Read(REG_RF09_AUXS);
		current_reg &= 0xFC;
		current_reg += PAVC;
		AT86RF215Write(REG_RF09_AUXS, current_reg);
	}
	else
	{
		uint8_t current_reg = AT86RF215Read(REG_RF24_AUXS);
		current_reg &= 0xFC;
		current_reg += PAVC;
		AT86RF215Write(REG_RF24_AUXS, current_reg);
	}
}

void AT86RF215TxSetPAC(uint8_t PAC)
{
	PAC &= 0x03;
	PAC = PAC << 5;
	if (AT86RF215.RF_Settings.Modem == MODEM_09){
		uint8_t current_reg = AT86RF215Read(REG_RF09_PAC);
		current_reg &= 0x9F;
		current_reg += PAC;
		AT86RF215Write(REG_RF09_PAC, current_reg);
	}
	else
	{
		uint8_t current_reg = AT86RF215Read(REG_RF24_PAC);
		current_reg &= 0x9F;
		current_reg += PAC;
		AT86RF215Write(REG_RF24_PAC, current_reg);
	}
}

void AT86RF215TxSetPwr(uint8_t PWR)
{
	PWR &= 0x1F;
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
//        if (AT86RF215.RF_Settings.Power != PWR){
    	uint8_t current_reg = AT86RF215Read(REG_RF09_PAC);
        current_reg &= 0xE0;
        current_reg += PWR;
        AT86RF215Write(REG_RF09_PAC, current_reg);
//        }
    } else{
//        if (AT86RF215.RF_Settings.Power != PWR){
    	uint8_t current_reg = AT86RF215Read(REG_RF24_PAC);
        current_reg &= 0xE0;
        current_reg += PWR;
        AT86RF215Write(REG_RF24_PAC, current_reg);
//        }
    }
    AT86RF215.RF_Settings.Power = PWR;
}

void AT86RF215TxSetSR(uint8_t TXSR)
{
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        if (AT86RF215.RF_Settings.TXSR != TXSR){
        	/* mask the SR */
        	TXSR &= 0x0F;
            uint8_t current_reg = AT86RF215Read(REG_RF09_TXDFE);
            current_reg &= 0xF0;
            current_reg += TXSR;
            AT86RF215Write(REG_RF09_TXDFE, current_reg);
        }
    } else if (AT86RF215.RF_Settings.Modem == MODEM_24){
        if (AT86RF215.RF_Settings.TXSR != TXSR){
        	/* mask the SR */
        	TXSR &= 0x0F;
            uint8_t current_reg = AT86RF215Read(REG_RF24_TXDFE);
            current_reg &= 0xF0;
            current_reg += TXSR;
            AT86RF215Write(REG_RF24_TXDFE, current_reg);
        }
    }
    else
    {
    	PrintError(ERROR_Modem);
    	return;
    }
    AT86RF215.RF_Settings.TXSR = TXSR;
}


void AT86RF215TxSetDirectMod(bool DM)
{
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        if (AT86RF215.BBC_Settings.directMod != DM){
            if (DM == true){
            	/* Set FSK direct modulation */
                AT86RF215Write(REG_BBC0_FSKDM, 0x01);
//                bitWrite(REG_RF09_TXDFE, 4, 1);
            }
            else{
                AT86RF215Write(REG_BBC0_FSKDM, 0x00);
//                bitWrite(REG_RF09_TXDFE, 4, 0);
            }
        }
    } else{
        if (AT86RF215.BBC_Settings.directMod != DM){
            if (DM == true){
            	/* Set FSK direct modulation */
                AT86RF215Write(REG_BBC0_FSKDM, 0x01);
//                bitWrite(REG_RF24_TXDFE, 4, 1);
            }
            else{
                AT86RF215Write(REG_BBC0_FSKDM, 0x00);
//                bitWrite(REG_RF24_TXDFE, 4, 0);
            }
        }
    }
    AT86RF215.BBC_Settings.directMod = DM;
}


void AT86RF215TxSetDataWhite(bool DW)
{
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        if (AT86RF215.BBC_Settings.dataWhite != DW){
            if (DW == true){
                bitWrite(REG_BBC0_FSKPHRTX, 2, 1);
            }
            else{
                bitWrite(REG_BBC0_FSKPHRTX, 2, 0);
            }
        }

    } else{
        if (AT86RF215.BBC_Settings.dataWhite != DW){
            if (DW == true){
                bitWrite(REG_BBC1_FSKPHRTX, 2, 1);
            }
            else{
                bitWrite(REG_BBC0_FSKPHRTX, 2, 0);
            }
        }
    }
    AT86RF215.BBC_Settings.dataWhite = DW;
}

void AT86RF215TxSetContinuous(bool CTX)
{
    /* Set or clear continuous transmission */
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        if (AT86RF215.RF_Settings.CTX != CTX){
            bitWrite(REG_BBC0_PC, 7, 1);
        }
    } else{
        if (AT86RF215.RF_Settings.CTX != CTX){
            bitWrite(REG_BBC1_PC, 7, 1);
        }
    }

    AT86RF215.RF_Settings.CTX = CTX;
}

//void AT86RF215TxSetBW(uint8_t TXBW)
//{
//    TXBW &= 0x0F;
//    if (AT86RF215.RF_Settings.Modem == MODEM_09){
//        if (AT86RF215.RF_Settings.TXBW != TXBW){
//            uint8_t current_reg = AT86RF215Read(REG_RF09_TXBWC);
//            current_reg &= 0xF0;
//            current_reg += TXBW;
//            AT86RF215Write(REG_RF09_TXBWC, current_reg);
//        }
//    } else{
//        if (AT86RF215.RF_Settings.TXBW != TXBW){
//            uint8_t current_reg = AT86RF215Read(REG_RF24_TXBWC);
//            current_reg &= 0xF0;
//            current_reg += TXBW;
//            AT86RF215Write(REG_RF24_TXBWC, current_reg);
//        }
//    }
//    AT86RF215.RF_Settings.TXBW = TXBW;
//}

void AT86RF215TxSetCutOff(uint8_t TXCUTOFF)
{
    TXCUTOFF &= 0x07;
    TXCUTOFF = TXCUTOFF << 5;
    if (AT86RF215.RF_Settings.Modem == MODEM_09){
        if (AT86RF215.RF_Settings.TXCUTOFF != TXCUTOFF){
            uint8_t current_reg = AT86RF215Read(REG_RF09_TXDFE);
            current_reg &= 0x1F;
            current_reg += TXCUTOFF;
            AT86RF215Write(REG_RF09_TXDFE, current_reg);
        }
    } else{
        if (AT86RF215.RF_Settings.TXCUTOFF != TXCUTOFF){
            uint8_t current_reg = AT86RF215Read(REG_RF24_TXDFE);
            current_reg &= 0x1F;
            current_reg += TXCUTOFF;
            AT86RF215Write(REG_RF24_TXDFE, current_reg);
        }
    }
    AT86RF215.RF_Settings.TXCUTOFF = TXCUTOFF;
}

void AT86RF215TxSetIQ(uint32_t freq)
{
    uint8_t current_state = AT86RF215GetState();
    printf("current_state: %x\n", current_state);

    if (current_state != RF_STATE_TRXOFF)
        AT86RF215SetState(RF_CMD_TRXOFF);

    /* set RF_IQIFC1 RF mode */
    AT86RF215SetRFMode(RF_MODE_RF);

    AT86RF215SetChannel(freq);

    /* PA current setting */
    AT86RF215TxSetPAC(RF_PAC_3dB_Reduction);

    /* PA DC voltage */
//    AT86RF215TxSetPAVC(RF_PA_VC_2_4);
    AT86RF215TxSetPAVC(RF_PA_VC_2_2);

    /* PA power */
    AT86RF215TxSetPwr(31);

    /* set sampling rate */
    AT86RF215TxSetSR(RF_SR4000);

    /* set cut-off frequency */
    AT86RF215TxSetCutOff(RF_CUT_4_4);

    /* set IRQ Mask */
    AT86RF215SetIRQMask(true, RF_IRQM_TRXRDY);
    AT86RF215SetIRQMask(false, RF_IRQM_WAKEUP);
    AT86RF215SetIRQMask(true, RF_IRQM_IQIFSF);

    uint8_t mask = AT86RF215Read(REG_RF09_IRQM);
//    printf("mask: %x\n", mask);

    uint8_t PAC = AT86RF215Read(REG_RF09_PAC);
//    printf("PAC: %x\n", PAC);

    uint8_t AUXS = AT86RF215Read(REG_RF09_AUXS);
//    printf("AUXS: %x\n", AUXS);

    /* set transmit mode */
    AT86RF215SetState(RF_CMD_TXPREP);

    /* read state */
//    current_state = AT86RF215GetState();
//    printf("current_state after : %x\n", current_state);
}

/**########################Internal functions############################**/
void AT86RF215Calibrate_LO(void)
{
	uint8_t temp[TRIM_LOOPS][2];
	bool reduced_measurements = true;
	uint16_t avg[2] = {0, 0};
	uint8_t TXCI = 0x00;
	uint8_t TXCQ = 0x00;

	if (AT86RF215.RF_Settings.Modem == MODEM_09)
	{
		/* Go to TRXOFF */
		AT86RF215SetState(RF_STATE_TRXOFF);

		uint8_t i;
		for(i=0; i<TRIM_LOOPS; i++)
		{
			/* Go to TXPREP */
			AT86RF215SetState(RF_STATE_TXPREP);
			/* wait for state */
			//ToDo?
			/* Go to TRXOFF */
			AT86RF215SetState(RF_STATE_TRXOFF);
			/* update state */
			AT86RF215.RF_Settings.State = RF_TRXOFF;

			/* read TXCI and TXCQ */
			temp[i][0] = AT86RF215Read(REG_RF09_TXCI);
			temp[i][1] = AT86RF215Read(REG_RF09_TXCQ);

			/* Check if the short loop measurement is sufficient */
			if (i == (NUM_SUFFICIENT_MEASUREMENTS - 1)) {
				uint8_t val[3];
				uint8_t k;
				for (k = 0; k < 2; k++)
				{
					val[0] = abs(temp[0][k] - temp[1][k]);
					val[1] = abs(temp[0][k] - temp[2][k]);
					val[2] = abs(temp[1][k] - temp[2][k]);
					uint8_t j;
					for (j=0; j<NUM_SUFFICIENT_MEASUREMENTS; j++)
					{
						if (val[j] > NARROW_TRIM_THRESHOLD) {
							reduced_measurements = false;
							break;
						}
					}
					if (reduced_measurements == false) {
						break;
					}
				}
				if (reduced_measurements == true) {
					/* Do stop measuring - do no more trim loops */
					break;
				}
			}
		}

		if (reduced_measurements == true) {
			/* Round value */
			uint8_t i;
			for (i = 0; i < NUM_SUFFICIENT_MEASUREMENTS; i++)
			{
				avg[0] += temp[i][0];
				avg[1] += temp[i][1];
			}
			TXCI = (uint8_t)(((float)avg[0] / NUM_SUFFICIENT_MEASUREMENTS) + 0.5);
			TXCQ = (uint8_t)(((float)avg[1] / NUM_SUFFICIENT_MEASUREMENTS) + 0.5);

		}
		else
		{ /* if (reduced_measurements == false) */
			int arr[TRIM_LOOPS];
			uint8_t i;

			for (i=0; i<TRIM_LOOPS; i++)
			{
				arr[i] = temp[i][0];
			}
			//TODO: median
//			TXCI = median(arr, TRIM_LOOPS);

			for (i=0; i<TRIM_LOOPS; i++)
			{
				arr[i] = temp[i][1];
			}
			//TODO: median
//			TXCQ = median(arr, TRIM_LOOPS);
		}

		//Update TXCI and TXCQ
		AT86RF215SetTXCI(TXCI);
		AT86RF215SetTXCQ(TXCQ);
	}
	else
	{
		//TODO
	}

}

void AT86RF215AGCEnable(bool state)
{
	if (AT86RF215.RF_Settings.Modem == MODEM_09)
	{
		if (state)
			bitWrite(REG_RF09_AGCC, 0x00, 0x01);
		else
			bitWrite(REG_RF09_AGCC, 0x00, 0x00);
	}
	else
	{
		//TODO
	}
}

void AT86RF215AGCSetGCW(uint8_t gcw)
{
	AT86RF215AGCEnable(false);
	gcw &= 0x1F;
	if (AT86RF215.RF_Settings.Modem == MODEM_09){
		uint8_t current_reg = AT86RF215Read(REG_RF09_AGCS);
		current_reg &= 0xE0;
		current_reg += gcw;
		AT86RF215Write(REG_RF09_AGCS, current_reg);
	}
	else if (AT86RF215.RF_Settings.Modem == MODEM_24){
		uint8_t current_reg = AT86RF215Read(REG_RF24_AGCS);
		current_reg &= 0xE0;
		current_reg += gcw;
		AT86RF215Write(REG_RF24_AGCS, current_reg);
	}
	else{
		PrintError(ERROR_Frequency);
	}
}

void AT86RF215AGCSetAGCC(bool agci, uint8_t agc_average)
{
	agc_average &= 0x03;
	agc_average = agc_average << 4;
	if (AT86RF215.RF_Settings.Modem == MODEM_09)
	{
		if (agci)	//unfiltered signal to AGC input
			bitWrite(REG_RF09_AGCC, 6, 1);
		else		//filtered signal to AGC input
			bitWrite(REG_RF09_AGCC, 6, 0);

		uint8_t current_reg = AT86RF215Read(REG_RF09_AGCC);
		current_reg &= 0xCF;
		current_reg += agc_average;
		AT86RF215Write(REG_RF09_AGCC, current_reg);
	}
	else if (AT86RF215.RF_Settings.Modem == MODEM_24)
	{
		if (agci)
			bitWrite(REG_RF24_AGCC, 6, 1);
		else
			bitWrite(REG_RF24_AGCC, 6, 0);

		uint8_t current_reg = AT86RF215Read(REG_RF24_AGCC);
		current_reg &= 0xCF;
		current_reg += agc_average;
		AT86RF215Write(REG_RF24_AGCC, current_reg);
	}
	else
	{
		PrintError(ERROR_Modem);
		return;
	}
}

void AT86RF215AGCSetTGT(uint8_t tgt)
{
	/* Enable Automatic Gain */
	AT86RF215AGCEnable(true);

	tgt &= 0x07;
	tgt = tgt << 5;
	if (AT86RF215.RF_Settings.Modem == MODEM_09)
	{
		uint8_t current_reg = AT86RF215Read(REG_RF09_AGCS);
		current_reg &= 0x1F;
		current_reg += tgt;
		AT86RF215Write(REG_RF09_AGCS, current_reg);

		current_reg = AT86RF215Read(REG_RF09_AGCS);
	}
	else
	{
		//TODO
	}
}

void AT86RF215SetIRQMask(bool status, uint8_t pos)
{
	uint8_t newVal;
	if (status)
		newVal = 0x01;
	else
		newVal = 0x00;
	if (AT86RF215.RF_Settings.Modem == MODEM_09)
	{
		bitWrite(REG_RF09_IRQM, pos, newVal);
	}
	else if (AT86RF215.RF_Settings.Modem == MODEM_24)
	{
		bitWrite(REG_RF24_IRQM, pos, newVal);
	}
	else {
		PrintError(ERROR_Modem);
	}
}

void AT86RF215SetOpMode( uint8_t mode )
{
    uint16_t addr;
    uint8_t data = mode & 0x07;

    if (RADIO_TYPE == RADIO_09)
        addr = REG_RF09_CMD;
    else if (RADIO_TYPE == RADIO_24)
        addr = REG_RF24_CMD;
    else
        addr = REG_RF09_CMD;

    AT86RF215Write(addr, data);
}

void bitWrite(uint16_t addr, uint8_t pos, uint8_t newValue)
{
    uint8_t current_value;
    current_value = AT86RF215Read(addr);

    uint8_t mask = 0x01 << pos;
    mask = ~mask;

    newValue = newValue << pos;

    current_value = current_value & mask;
    newValue |= current_value;

    AT86RF215Write(addr, newValue);
}

uint8_t bitRead(uint16_t addr, uint8_t pos)
{
    uint8_t ret;
    ret = AT86RF215Read(addr);
    ret >>= pos;
    ret &= 0x01;

    return ret;
}

void AT86RF215SetIQSkewDrive(uint8_t skew)
{
	skew &= 0x03;
	uint8_t current_reg = AT86RF215Read(REG_RF_IQIFC1);
	current_reg &= 0xFC;
	current_reg += skew;
	AT86RF215Write(REG_RF_IQIFC1, current_reg);
	current_reg = AT86RF215Read(REG_RF_IQIFC1);
}

void AT86RF215SetFEConfiguration(uint8_t FE)
{
	FE &= 0x03;
	FE = FE << 0x06;
	if (AT86RF215.RF_Settings.Modem == MODEM_09){
		uint8_t current_reg = AT86RF215Read(REG_RF09_PADFE);
		current_reg &= 0x3F;
		current_reg += FE;
		AT86RF215Write(REG_RF09_PADFE, current_reg);
	} else if (AT86RF215.RF_Settings.Modem == MODEM_24){
		uint8_t current_reg = AT86RF215Read(REG_RF24_PADFE);
		current_reg &= 0x3F;
		current_reg += FE;
		AT86RF215Write(REG_RF24_PADFE, current_reg);
	} else {
		PrintError(ERROR_Modem);
	}
}

void AT86RF215SetIQCurrentDrive(uint8_t drive)
{
	drive &= 0x03;
	drive = drive << 4;
	uint8_t current_reg = AT86RF215Read(REG_RF_IQIFC0);
	current_reg &= 0xCF;
	current_reg += drive;
	AT86RF215Write(REG_RF_IQIFC0, current_reg);
}

void AT86RF215SetLVDSCMV(bool v1_2, uint8_t cmv)
{
	uint8_t current_reg;
	current_reg = AT86RF215Read(REG_RF_IQIFC0);

	if (v1_2)
	{
		bitWrite(REG_RF_IQIFC0, 1, 1);
	}
	else
	{
		bitWrite(REG_RF_IQIFC0, 1, 0);
		cmv &= 0x03;
		cmv = cmv << 2;
		current_reg = AT86RF215Read(REG_RF_IQIFC0);
		current_reg &= 0xF3;
		current_reg += cmv;
		AT86RF215Write(REG_RF_IQIFC0, current_reg);
	}

	current_reg = AT86RF215Read(REG_RF_IQIFC0);
}
