#! /bin/bash

ROOT_DIR=`pwd`
SOAPY_UTILS_EXE=SoapySDRUtil

# git clone 

# update the git repo on develop_R1 branch to include sub-modules
echo "-- CaribouLite Git Repo"
git checkout develop_R1
git submodule init
git submodule update

# clone SoapySDR dependencies
echo "-- Checking Soapy SDR installation ($SOAPY_UTILS_EXE)..."

SOAPY_UTIL_PATH=`which $SOAPY_UTILS_EXE`

if test -f "${SOAPY_UTIL_PATH}"; then
    echo "Found SoapySDRUtil at ${SOAPY_UTIL_PATH}"
else
    mkdir installations
    cd installations

    echo "Did not find SoapySDRUtil"
    read -p 'Do you want to clone and install? (Y/N): ' InstallSoapy
    
    if [ "$InstallSoapy" = "Y" ]; then
        echo "-- Cloning SoapySDR and compiling..."
        rm -R SoapySDR
        rm -R SoapyRemote
        git clone https://github.com/pothosware/SoapySDR.git
        git clone https://github.com/pothosware/SoapyRemote.git
        
        # Soapy
        cd SoapySDR
        mkdir build && cd build
        cmake ../
        make -j4 && make install        
        
        # Soapy Remote (Server)
        cd ../..
        cd SoapyRemote
        mkdir build && cd build
        cmake ../
        make -j4 && make install
    fi
    
    echo "-- Checking the installed Soapy utilities..."
    SOAPY_UTIL_PATH=`which $SOAPY_UTILS_EXE`
    if test -f "${SOAPY_UTIL_PATH}"; then
        echo "Found SoapySDRUtil at ${SOAPY_UTIL_PATH}"
    else
        echo "Did not find the newly installed SoapySDR package. Exiting..."
        cd ..
        exit 1
    fi
    
    cd ..
fi

## kernel module dev dependencies
echo "-- Updating system and installing uild dependencies..."
apt-get update
apt-get install raspberrypi-kernel-headers module-assistant pkg-config libncurses5-dev cmake git 
sudo depmod -a

## Main Software
echo "-- Compiling main source..."
echo "---- 1. External Tools..."
cd $ROOT_DIR/software/utils
cmake ./
make

echo "---- 2. SMI kernel module..."
cd $ROOT_DIR/software/libcariboulite/src/caribou_smi/kernel
mkdir build
cd build
cmake ../
make

echo "---- 3. Main software..."
cd $ROOT_DIR
mkdir build
cd build
cmake $ROOT_DIR/software/libcariboulite/
make
#make install

# Configuration File
echo "-- Environmental Settings..."
echo "---- 1. SPI configuration..."
DtparamSPI=`cat /boot/config.txt | grep "dtparam=spi" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtparam" ]; then
    echo "Warning - RespberryPi configuration file at '/boot/config.txt' contains SPI configuration"
    echo "          Please disable SPI by commenting out the line as follows: '#dtparam=spi=on'"
else
    echo "Okay :)"
fi

echo "---- 2. ARM I2C Configuration..."
DtparamSPI=`cat /boot/config.txt | grep "dtparam=i2c_arm" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtparam" ]; then
    echo "Warning - RespberryPi configuration file at '/boot/config.txt' contains ARM-I2C configuration"
    echo "          Please disable ARM-I2C by commenting out the line as follows: '#dtparam=i2c_arm=on'"
else
    echo "Okay :)"
fi

echo "---- 3. I2C-VC Configuration..."
DtparamSPI=`cat /boot/config.txt | grep "dtparam=i2c_vc" | xargs | cut -d\= -f1`
if [ "$DtparamSPI" = "dtparam" ]; then
    echo "Okay :)"
else
    echo "Warning - To communicate with CaribouLite EEPROM, the i2c_vc device needs to be enabled"
    echo "          Please add the following to the '/boot/config.txt' file: 'dtparam=i2c_vc=on'"
fi


## UDEV rules
## TODO

echo "Done!"
