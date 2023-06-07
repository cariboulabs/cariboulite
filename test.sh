#! /bin/bash

CONFIG_TXT_PATH="/boot/config.txt"

if grep -iq "NAME=\"Ubuntu\"" /etc/os-release; then
	CONFIG_TXT_PATH="/boot/firmware/config.txt"
	echo "config.txt file path is ${CONFIG_TXT_PATH} - DragonOS detected"
else	
	echo "config.txt file path is ${CONFIG_TXT_PATH} - Raspbian detected"
fi

printf "\n[  6  ] ${GREEN}Environmental Settings...${NC}\n"
printf "${GREEN}1. SPI configuration...  "
DtparamSPI=`cat ${CONFIG_TXT_PATH} | grep "dtparam=spi" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtparam" ]; then
    printf "${RED}Warning${NC}\n"
    printf "${RED}RespberryPi configuration file at ${CONFIG_TXT_PATH} contains SPI configuration${NC}\n"
    printf "${RED}Please disable SPI by commenting out the line as follows: '#dtparam=spi=on'${NC}\n"
    ERROR="1"
else
    printf "${CYAN}OK :)${NC}\n"
fi

printf "${GREEN}2. ARM I2C Configuration...  "
DtparamSPI=`cat ${CONFIG_TXT_PATH} | grep "dtparam=i2c_arm" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtparam" ]; then
    printf "${RED}Warning${NC}\n"
    printf "${RED}RespberryPi configuration file at ${CONFIG_TXT_PATH} contains ARM-I2C configuration${NC}\n"
    printf "${RED}Please disable ARM-I2C by commenting out the line as follows: '#dtparam=i2c_arm=on'${NC}\n"
    ERROR="1"
else
    printf "${CYAN}OK :)${NC}\n"
fi

printf "${GREEN}3. I2C-VC Configuration...  "
DtparamSPI=`cat ${CONFIG_TXT_PATH} | grep "dtparam=i2c_vc" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtparam" ]; then
    printf "${CYAN}OK :)${NC}\n"
else
    printf "${RED}Warning${NC}\n"
    printf "${RED}To communicate with CaribouLite EEPROM, the i2c_vc device needs to be enabled${NC}\n"
    printf "${RED}Please add the following to the ${CONFIG_TXT_PATH} file: 'dtparam=i2c_vc=on'${NC}\n"
    ERROR="1"
fi


printf "${GREEN}4. SPI1-3CS Configuration...  "
DtparamSPI=`cat ${CONFIG_TXT_PATH} | grep "dtoverlay=spi1-3cs" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtoverlay" ]; then
    printf "${CYAN}OK :)${NC}\n"
else
    printf "${RED}Warning${NC}\n"
    printf "${RED}To communicate with CaribouLite Modem, FPGA, etc, SPI1 (AUX) with 3CS needs to be to be enabled${NC}\n"
    printf "${RED}Please add the following to the ${CONFIG_TXT_PATH} file: 'dtoverlay=spi1-3cs'${NC}\n"
    ERROR="1"
fi
