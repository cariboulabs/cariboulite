#! /bin/bash

set -euo pipefail

ROOT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
KERNEL_RELEASE=$(uname -r)
RED='\033[0;31m'
GREEN='\033[1;32m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

BUILD_DIR="${ROOT_DIR}/build"
BLOB_CREATOR_DIR="${ROOT_DIR}/../software/utils"
USERSPACE_SMI_DIR="${ROOT_DIR}/../software/libcariboulite/src/caribou_smi/kernel"

[ $(id -u) = 0 ] && printf "${RED}Please do not run this script as root${NC}\n" && exit 100

## FUNCTIONS
install() {
    local mtu_mult=${1:-16}
    local dir_offs=${2:-2}
    local ch_offs=${3:-3}

    printf "${GREEN}Installation started...${NC}\n"
    printf "\n[  1  ] ${GREEN}Updating kernel headers and needed software${NC}\n"
    # Raspberry Pi OS now ships versioned Debian-style header packages.
    # Reuse installed headers so a kernel rebuild does not depend on apt/network.
    local packages=() package
    if [[ ! -f "/lib/modules/${KERNEL_RELEASE}/build/Makefile" ]]; then
        packages+=("linux-headers-${KERNEL_RELEASE}")
    fi
    for package in build-essential pkg-config cmake git kmod xz-utils; do
        if [[ $(dpkg-query -W -f='${Status}' "$package" 2>/dev/null || true) != 'install ok installed' ]]; then
            packages+=("$package")
        fi
    done
    if (( ${#packages[@]} )); then
        sudo apt-get update
        sudo apt-get -y install "${packages[@]}"
    fi

    printf "\n[  2  ] ${GREEN}Compiling module${NC}\n"
    if [ -d "$BUILD_DIR" ]; then
        echo "Subdirectory '$BUILD_DIR' exists. Deleting its contents..."
        rm -rf "$BUILD_DIR"/*
    else
        echo "Subdirectory '$BUILD_DIR' does not exist. Creating it..."
        mkdir "$BUILD_DIR"
    fi

    # Pin both compilation and installation to the same kernel release.
    cmake -S "${ROOT_DIR}" -B "$BUILD_DIR" \
        -DKERNEL_RELEASE="$KERNEL_RELEASE" \
        -DKERNELHEADERS_DIR="/lib/modules/${KERNEL_RELEASE}/build"
    cmake --build "$BUILD_DIR"

    local module="$BUILD_DIR/smi_stream_dev.ko"
    local vermagic
    vermagic=$(modinfo -F vermagic "$module")
    if [[ "${vermagic%% *}" != "$KERNEL_RELEASE" ]]; then
        printf '%s\n' "Error: built module kernel does not match $KERNEL_RELEASE" >&2
        exit 100
    fi

    # Search only this kernel, and reject ambiguous destinations.
    local destinations output_dir
    destinations=$(find "/lib/modules/$KERNEL_RELEASE" -type f \
        -name 'bcm2835_smi_dev.ko*' -printf '%h\n' | sort -u)
    if [[ -z "$destinations" || "$destinations" == *$'\n'* ]]; then
        printf '%s\n' "Error: expected one bcm2835_smi_dev directory for $KERNEL_RELEASE" >&2
        exit 100
    fi
    output_dir="$destinations"

    # Finish packaging before changing installed modules or boot configuration.
    xz -z "$module" -c > "$module.xz.tmp"
    mv "$module.xz.tmp" "$module.xz"
    "${BLOB_CREATOR_DIR}/generate_bin_blob" "$module" smi_stream_dev "${USERSPACE_SMI_DIR}/smi_stream_dev_gen.h"
    cp "${ROOT_DIR}/bcm2835_smi.h" "$USERSPACE_SMI_DIR/"
    cp "${ROOT_DIR}/smi_stream_dev.h" "$USERSPACE_SMI_DIR/"

    printf "\n[  3  ] ${GREEN}Installing into '${output_dir}'${NC}\n"
    sudo cp "$module.xz" "$output_dir/"

    printf "\n[  4  ] ${GREEN}Updating 'depmod'${NC}\n"
    sudo depmod -a "$KERNEL_RELEASE"

    printf "\n[  5  ] ${GREEN}Blacklisting original bcm2835_smi_dev module${NC}\n"
    echo "# blacklist the broadcom default smi module to replace with smi_stream_dev" | sudo tee "/etc/modprobe.d/blacklist-bcm_smi.conf" > /dev/null
    echo "blacklist bcm2835_smi_dev" | sudo tee -a "/etc/modprobe.d/blacklist-bcm_smi.conf" > /dev/null

    printf "\n[  6  ] ${GREEN}Adding systemd configuration${NC}\n"
    echo "# load SMI stream driver on startup" | sudo tee "/etc/modules-load.d/smi_stream_mod.conf" > /dev/null
    echo "smi_stream_dev" | sudo tee -a "/etc/modules-load.d/smi_stream_mod.conf" > /dev/null

    printf "\n[  7  ] ${GREEN}Adding modprobe configuration ${mtu_mult}, ${dir_offs}, ${ch_offs}${NC}\n"
    echo "# SMI STREAM DEV specific options" | sudo tee "/etc/modprobe.d/smi_stream_mod_cariboulite.conf" > /dev/null
    echo "options smi_stream_dev fifo_mtu_multiplier=${mtu_mult} addr_dir_offset=${dir_offs} addr_ch_offset=${ch_offs}" | sudo tee -a "/etc/modprobe.d/smi_stream_mod_cariboulite.conf" > /dev/null

    printf "\n[  8  ] ${GREEN}Adding UDEV rules${NC}\n"
    cd "${ROOT_DIR}/udev"
    sudo ./install.sh install
    cd "${ROOT_DIR}"

    printf "${GREEN}Installation completed.${NC}\n"
}

uninstall() {
    printf "${GREEN}Uninstalling started...${NC}\n"

    # find the location of the older installed module
    output_dir=$(find "/lib/modules" -type f -name "smi_stream_dev*" -exec dirname {} \;)

    if [ -z "$output_dir" ]; then
        printf "${CYAN}Warning: module 'smi_stream_dev' is not installed in the system${NC}\n"
        sudo depmod -a
        exit 0
    fi

    printf "\n[  1  ] ${GREEN}Uninstalling from '${output_dir}'${NC}\n"
    sudo rm ${output_dir}/smi_stream_dev.ko.xz

    printf "\n[  2  ] ${GREEN}Updating 'depmod'${NC}\n"
    sudo depmod -a

    printf "\n[  3  ] ${GREEN}Removing the blacklist on the legacy smi device${NC}\n"
    if [ -f "/etc/modprobe.d/blacklist-bcm_smi.conf" ]; then
        sudo rm "/etc/modprobe.d/blacklist-bcm_smi.conf"
    fi

    printf "\n[  4  ] ${GREEN}Removing device driver loading on start${NC}\n"
    if [ -f "/etc/modules-load.d/smi_stream_mod.conf" ]; then
        sudo rm "/etc/modules-load.d/smi_stream_mod.conf"
    fi

    printf "\n[  5  ] ${GREEN}Removing modprobe parameters${NC}\n"
    if [ -f "/etc/modprobe.d/smi_stream_mod_cariboulite.conf" ]; then
        sudo rm "/etc/modprobe.d/smi_stream_mod_cariboulite.conf"
    fi

    printf "\n[  6  ] ${GREEN}Removing UDEV rules${NC}\n"
    sudo "${ROOT_DIR}/udev/install.sh" uninstall

    printf "${GREEN}Uninstallation completed.${NC}\n"
}

## FLOW
printf "${GREEN}CaribouLite Device Driver Install / Uninstall${NC}\n"
printf "${GREEN}=============================================${NC}\n\n"

if [ "${1:-}" == "install" ]; then
    install "${2:-16}" "${3:-2}" "${4:-3}"

    exit 0
elif [ "${1:-}" == "uninstall" ]; then
    uninstall

    exit 0
else
    printf "${CYAN}Usage: $0 [install|uninstall] <mtu_mult dir_offs ch_offs>${NC}\n"
    exit 1
fi

## Say that restart is needed!
print "${GREEN}Now the RPI needs to be restarted...${NC}\n"
