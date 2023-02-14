#! /bin/bash

ROOT_DIR=`pwd`
SOAPY_UTILS_EXE=SoapySDRUtil
RED='\033[0;31m'
GREEN='\033[1;32m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color
ERROR="0"

# update the git repo on develop_R1 branch to include sub-modules
printf "\n[  1  ] ${GREEN}CaribouLite Git Repo${NC}\n"
git checkout develop_R1
git pull
git submodule init
git submodule update

## kernel module dev dependencies
printf "\n[  2  ] ${GREEN}Updating system and installing dependencies...${NC}\n"
sudo -u root apt-get update
sudo -u root apt-get install raspberrypi-kernel-headers module-assistant pkg-config libncurses5-dev cmake git libzmq3-dev avahi-daemon libavahi-client-dev
sudo -u root depmod -a

# clone SoapySDR dependencies
printf "\n[  3  ] ${GREEN}Checking Soapy SDR installation ($SOAPY_UTILS_EXE)...${NC}\n"

SOAPY_UTIL_PATH=`which $SOAPY_UTILS_EXE`

if test -f "${SOAPY_UTIL_PATH}"; then
    printf "${CYAN}Found SoapySDRUtil at ${SOAPY_UTIL_PATH}${NC}\n"
else
    mkdir installations
    cd installations

    printf "${RED}Did not find SoapySDRUtil${NC}. Do you want to clone and install? (Y/[N]):"
    read -p ' ' InstallSoapy
    
    if [ "$InstallSoapy" = "Y" ]; then
        printf "==> ${GREEN}Cloning SoapySDR, and compiling...${NC}\n"
        rm -R SoapySDR
        git clone https://github.com/pothosware/SoapySDR.git
        
        
        # Soapy
        cd SoapySDR
        mkdir build && cd build
        cmake ../
        make -j4 && sudo -u root make install
        sudo -u root ldconfig
    fi

    SOAPYMODPATH=`SoapySDRUtil --info | grep "Search path" | cut -d":" -f2 | xargs | cut -d" " -f1`
    SOAPYMODPATH_PREFIX=`SoapySDRUtil --info | grep "Search path" | cut -d":" -f2 | xargs | cut -d" " -f1 | awk -F '/Soapy' '{print $1}'`
    sudo -u root mkdir -p $SOAPYMODPATH
    
    #cmake -DCMAKE_INSTALL_PREFIX:PATH=/usr ../ && make all install
    
    if [ "$InstallSoapy" = "Y" ]; then
        printf "==> ${GREEN}Cloning SoapyRemote, and compiling...${NC}\n"
        rm -R SoapyRemote
        git clone https://github.com/pothosware/SoapyRemote.git
        
        # Soapy Remote (Server)
        cd ../..
        cd SoapyRemote
        mkdir build && cd build
        cmake -DCMAKE_INSTALL_PREFIX:PATH=$SOAPYMODPATH_PREFIX ../
        make -j4 && sudo -u root make install
        sudo -u root ldconfig
    fi
    
    printf "\n[  4  ] ${GREEN}Checking the installed Soapy utilities...${NC}\n"
    SOAPY_UTIL_PATH=`which $SOAPY_UTILS_EXE`
    if test -f "${SOAPY_UTIL_PATH}"; then
        printf "${CYAN}Found SoapySDRUtil at ${SOAPY_UTIL_PATH}${NC}\n"
    else
        printf "\n${RED}Failed installing Soapy. Exiting...${NC}\n\n"
        cd ..
        exit 1
    fi
    
    cd ..
fi

## Main Software
printf "\n[  5  ] ${GREEN}Compiling main source...${NC}\n"
printf "${CYAN}1. External Tools...${NC}\n"
cd $ROOT_DIR/software/utils
mkdir build && cd build
cmake ../
make
mv $ROOT_DIR/software/utils/build/generate_bin_blob $ROOT_DIR/software/utils/generate_bin_blob

printf "${CYAN}2. SMI kernel module...${NC}\n"
cd $ROOT_DIR/software/libcariboulite/src/caribou_smi/kernel
mkdir build
cd build
cmake ../
make

printf "${CYAN}3. Main software...${NC}\n"
cd $ROOT_DIR
mkdir build
cd build
cmake $ROOT_DIR/software/libcariboulite/
make
sudo -u root make install

# Configuration File
printf "\n[  6  ] ${GREEN}Environmental Settings...${NC}\n"
printf "${GREEN}1. SPI configuration...  "
DtparamSPI=`cat /boot/config.txt | grep "dtparam=spi" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtparam" ]; then
    printf "${RED}Warning${NC}\n"
    printf "${RED}RespberryPi configuration file at '/boot/config.txt' contains SPI configuration${NC}\n"
    printf "${RED}Please disable SPI by commenting out the line as follows: '#dtparam=spi=on'${NC}\n"
    ERROR="1"
else
    printf "${CYAN}OK :)${NC}\n"
fi

printf "${GREEN}2. ARM I2C Configuration...  "
DtparamSPI=`cat /boot/config.txt | grep "dtparam=i2c_arm" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtparam" ]; then
    printf "${RED}Warning${NC}\n"
    printf "${RED}RespberryPi configuration file at '/boot/config.txt' contains ARM-I2C configuration${NC}\n"
    printf "${RED}Please disable ARM-I2C by commenting out the line as follows: '#dtparam=i2c_arm=on'${NC}\n"
    ERROR="1"
else
    printf "${CYAN}OK :)${NC}\n"
fi

printf "${GREEN}3. I2C-VC Configuration...  "
DtparamSPI=`cat /boot/config.txt | grep "dtparam=i2c_vc" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtparam" ]; then
    printf "${CYAN}OK :)${NC}\n"
else
    printf "${RED}Warning${NC}\n"
    printf "${RED}To communicate with CaribouLite EEPROM, the i2c_vc device needs to be enabled${NC}\n"
    printf "${RED}Please add the following to the '/boot/config.txt' file: 'dtparam=i2c_vc=on'${NC}\n"
    ERROR="1"
fi

## UDEV rules
# Still the /dev/mem problem. Un-restricting the CONFIG_STRICT_DEVMEM kernel config option doesn't
# help. Neither adding "pi" to the kmem, dialout and mem groups. pigpiod may be the last resort.

if [ "$ERROR" = "1" ]; then
    printf "\n[  7  ] ${RED}Installation errors occured.${NC}\n\n\n"
else
    printf "\n[  7  ] ${GREEN}All went well. Please reboot the system to finalize installation...${NC}\n\n\n"
fi
