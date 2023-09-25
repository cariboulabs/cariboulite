#! /bin/bash

## --------------------------------------------------------------------
## Variables
## --------------------------------------------------------------------
ROOT_DIR=`pwd`
SOAPY_UTILS_EXE=SoapySDRUtil
RED='\033[0;31m'
GREEN='\033[1;32m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color
ERROR="0"

[ $(id -u) = 0 ] && echo "Please do not run this script as root" && exit 100

## --------------------------------------------------------------------
## update the git repo on develop_R1 branch to include sub-modules
## --------------------------------------------------------------------
printf "\n[  1  ] ${GREEN}CaribouLite Git Repo${NC}\n"
git pull
git submodule init
git submodule update

## --------------------------------------------------------------------
## kernel module dev dependencies
## --------------------------------------------------------------------
printf "\n[  2  ] ${GREEN}Updating system and installing dependencies...${NC}\n"
sudo apt-get update
sudo apt-get -y install raspberrypi-kernel-headers # raspbian
sudo apt-get -y install linux-headers-raspi # ubuntu
sudo apt-get -y install module-assistant pkg-config libncurses5-dev cmake git libzmq3-dev
sudo apt-get -y install swig avahi-daemon libavahi-client-dev python3-distutils libpython3-dev

# In ubuntu we need to grant access to gpiomem
if grep -iq "NAME=\"Ubuntu\"" /etc/os-release; then
	sudo apt-get install rpi.gpio-common
	echo "Adding user `whoami` to dialout, root groups"
	
	if [[ "`groups ``whoami`" == *`whoami`* ]]; then
	   echo "`   User already in the group`" 
	else
	   sudo usermod -aG dialout, root "${USER}"
	fi
	
fi

sudo depmod -a

## --------------------------------------------------------------------
## clone SoapySDR dependencies
## --------------------------------------------------------------------
printf "\n[  3  ] ${GREEN}Checking Soapy SDR installation ($SOAPY_UTILS_EXE)...${NC}\n"

SOAPY_UTIL_PATH=`which $SOAPY_UTILS_EXE`

if test -f "${SOAPY_UTIL_PATH}"; then
    printf "${CYAN}Found SoapySDRUtil at ${SOAPY_UTIL_PATH}${NC}\n"
else
    mkdir -p $ROOT_DIR/installations

    printf "${RED}Did not find SoapySDRUtil${NC}. Do you want to clone and install? [Y/n]: "
    read -s -n1
    REPLY="${REPLY}Y <- default"

    if [ "$REPLY" == "${REPLY#[Yy]}" ]; then
        printf "N\n"
    else
        printf "Y\n"

        printf "==> ${GREEN}Cloning SoapySDR, and compiling...${NC}\n"
        cd $ROOT_DIR/installations

        rm -rf SoapySDR
        git clone https://github.com/pothosware/SoapySDR.git

        # Soapy SDR -- the SDR abstraction library
        cd SoapySDR
        mkdir -p build && cd build
        cmake ../
        make && sudo make install
        sudo ldconfig

        printf "==> ${GREEN}Cloning SoapyRemote, and compiling...${NC}\n"
        cd $ROOT_DIR/installations

        rm -rf SoapyRemote
        git clone https://github.com/pothosware/SoapyRemote.git

        # Soapy Server -- Use any Soapy SDR remotely
        cd SoapyRemote
        mkdir -p build && cd build
        cmake ../
        make && sudo make install
        sudo ldconfig
    fi

    printf "\n[  4  ] ${GREEN}Checking the installed Soapy utilities...${NC}\n"
    SOAPY_UTIL_PATH=`which $SOAPY_UTILS_EXE`
    if test -f "${SOAPY_UTIL_PATH}"; then
        printf "${CYAN}Found SoapySDRUtil at ${SOAPY_UTIL_PATH}${NC}\n"
    else
        printf "\n${RED}Failed installing Soapy. Exiting...${NC}\n\n"
        exit 1
    fi
fi

## --------------------------------------------------------------------
## Main Software
## --------------------------------------------------------------------
printf "\n[  5  ] ${GREEN}Compiling main source...${NC}\n"
printf "${CYAN}1. External Tools...${NC}\n"
cd $ROOT_DIR/software/utils
mkdir -p build && cd build
cmake ../
make
mv $ROOT_DIR/software/utils/build/generate_bin_blob $ROOT_DIR/software/utils/generate_bin_blob

printf "${CYAN}2. libIIR ${NC}\n"
cd $ROOT_DIR/software/libcariboulite/src/iir/
mkdir -p build && cd build
cmake ../
make
sudo make install
sudo ldconfig

printf "${CYAN}3. SMI kernel module & udev...${NC}\n"
cd $ROOT_DIR/driver
kernel_memory=$(grep "MemAvailable:" /proc/meminfo | awk '{print $2}')
kernel_memory_mb=$((kernel_memory / 1024))
printf "${CYAN}   Detected memory ${kernel_memory_mb} MB...${NC}\n"
if (( kernel_memory_mb > 512 )); then
  printf "${CYAN}   Installing with Fifo size multiplier of 6xMTU...${NC}\n"
  ./install.sh install 6 2 3
else
  printf "${CYAN}   Installing with Fifo size multiplier of 2xMTU...${NC}\n"
  ./install.sh install 2 2 3
fi
cd ..

printf "${CYAN}4. Main software...${NC}\n"
cd $ROOT_DIR
mkdir -p build && cd build
cmake $ROOT_DIR/software/libcariboulite/
make
sudo make install

## --------------------------------------------------------------------
## Configuration File - RPI /boot/config.txt
## --------------------------------------------------------------------
CONFIG_TXT_PATH="/boot/config.txt"
if grep -iq "NAME=\"Ubuntu\"" /etc/os-release; then
	CONFIG_TXT_PATH="/boot/firmware/config.txt"
fi

printf "\n[  6  ] ${GREEN}Environmental Settings...${NC}\n"
printf "${GREEN}1. SPI configuration...  "
DtparamSPI=`grep "dtparam=spi" "${CONFIG_TXT_PATH}" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtparam" ]; then
    printf "${RED}Warning${NC}\n"
    printf "${RED}RespberryPi configuration file at ${CONFIG_TXT_PATH} contains SPI configuration${NC}\n"
    printf "${RED}Please disable SPI by commenting out the line as follows: '#dtparam=spi=on'${NC}\n"
    ERROR="1"
else
    printf "${CYAN}OK :)${NC}\n"
fi

printf "${GREEN}2. ARM I2C Configuration...  "
DtparamSPI=`grep "dtparam=i2c_arm" "${CONFIG_TXT_PATH}" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtparam" ]; then
    printf "${RED}Warning${NC}\n"
    printf "${RED}RespberryPi configuration file at ${CONFIG_TXT_PATH} contains ARM-I2C configuration${NC}\n"
    printf "${RED}Please disable ARM-I2C by commenting out the line as follows: '#dtparam=i2c_arm=on'${NC}\n"
    ERROR="1"
else
    printf "${CYAN}OK :)${NC}\n"
fi

printf "${GREEN}3. I2C-VC Configuration...  "
DtparamSPI=`grep "dtparam=i2c_vc" "${CONFIG_TXT_PATH}" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtparam" ]; then
    printf "${CYAN}OK :)${NC}\n"
else
    printf "${RED}Warning${NC}\n"
    printf "${RED}To communicate with CaribouLite EEPROM, the i2c_vc device needs to be enabled${NC}\n"
    printf "${RED}Please add the following to the ${CONFIG_TXT_PATH} file: 'dtparam=i2c_vc=on'${NC}\n"
    ERROR="1"
fi


printf "${GREEN}4. SPI1-3CS Configuration...  "
DtparamSPI=`grep "dtoverlay=spi1-3cs" "${CONFIG_TXT_PATH}" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtoverlay" ]; then
    printf "${CYAN}OK :)${NC}\n"
else
    printf "${RED}Warning${NC}\n"
    printf "${RED}To communicate with CaribouLite Modem, FPGA, etc, SPI1 (AUX) with 3CS needs to be to be enabled${NC}\n"
    printf "${RED}Please add the following to the ${CONFIG_TXT_PATH} file: 'dtoverlay=spi1-3cs'${NC}\n"
    ERROR="1"
fi


if [ "$ERROR" = "1" ]; then
    printf "\n[  7  ] ${RED}Installation errors occured.${NC}\n\n\n"
else
    printf "\n[  7  ] ${GREEN}All went well. Please reboot the system to finalize installation...${NC}\n\n\n"
fi
