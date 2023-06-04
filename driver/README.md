# SMI Stream Driver
SMI streaming async module. Previously located under the userspace driver 'caribou_smi' and was integrated into it. Currently an autonomous driver to be loaded by the kernel in runtime.
The driver replaces the legacy broadcom SMI driver.

# Installation
Use `../install.sh` to start the installation sequence:
1. Update kernel headers - this kernel driver needs to be "updated" on each kernel update done on the system. This means that the `install.sh` should be run again. This part in the sequence required internet connectivity.
2. Create the build directory, and build the kernel module.
3. Installing the ko.xz file into the `/ilb/modules` directory + `depmod`
4. TBD

# Installing UDEV Rules
// The artifacts - TBD

