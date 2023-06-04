#! /bin/bash


## FUNCTIONS
install() {
    printf "Installing UDEV rules...\n"
    sudo cp 40-cariboulite.rules /etc/udev/rules.d/
    sudo udevadm control --reload-rules && udevadm trigger
    printf "Installation finished\n"
}

uninstall() {
    printf "Uninstalling UDEV rules...\n"
    if [ -f "/etc/udev/rules.d/40-cariboulite.rules" ]; then
        sudo rm "/etc/udev/rules.d/40-cariboulite.rules"
    fi
    sudo udevadm control --reload-rules && udevadm trigger
    printf "Uninstallation finished\n"
}


## FLOW
printf "${GREEN}CaribouLite UDEV Rules (un)installation${NC}\n"
printf "${GREEN}=======================================${NC}\n\n"

if [ "$1" == "install" ]; then
    install
    
    exit 0
elif [ "$1" == "uninstall" ]; then
    uninstall
    
    exit 0
else
    printf "${CYAN}Usage: $0 [install|uninstall]${NC}\n"
    exit 1
fi