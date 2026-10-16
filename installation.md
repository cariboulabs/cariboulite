# Installation

These instructions describe the current installer in this development fork.
The older blanket recommendation to use Bullseye because Bookworm cannot build
the driver is obsolete. A fresh 64-bit Raspberry Pi OS Trixie installation on
a Pi Zero 2 W was reported successful on 14 October 2026; see the
[installation and audio notes](ADDITIONAL-README.md). Pi 4 and Pi Zero 2 W
hardware-paced TX checks with kernel `6.18.50+rpt-rpi-v8` are recorded in the
[Pi 4 validation](docs/pi4-tx-validation-2026-10-15.md) and
[Pi Zero validation](docs/pi-zero-tx-queue-test-2026-10-15.md).
Those results identify tested setups, rather than support for every OS/kernel
combination. Raspberry Pi 5 is not supported by this SMI hardware path.

## Prepare the Pi

Install a Raspberry Pi OS image suitable for your Pi with Raspberry Pi Imager.
Writing the image erases the selected card. Configure a user, hostname and SSH
or local desktop access, then boot the Pi. Power the Pi off before attaching
CaribouLite to its 40-pin header.

Install the checkout tools and obtain this fork, including its IIR submodule:

```sh
sudo apt update
sudo apt install git cmake
mkdir -p ~/src
cd ~/src
git clone --branch dev-passive-monitor --recurse-submodules https://github.com/Habraken/cariboulite.git
cd cariboulite
```

The branch above contains the standalone radio applications and hardware-paced
TX work reviewed on 16 October 2026. For an existing checkout, inspect
`git status` and your selected branch before updating. The installer itself
runs `git pull` and `git submodule update --init --recursive`.

The kernel build requires headers for the **running** kernel:

```sh
uname -r
ls -l "/lib/modules/$(uname -r)/build/Makefile"
```

If that Makefile is absent, the installer requests
`linux-headers-$(uname -r)`. Package availability depends on the installed
kernel and configured repositories. After a kernel update, reboot into the
intended kernel before building its SMI module.

## Run the installer

Run as your normal user; the script invokes `sudo` for system changes and
refuses to run as root:

```sh
./install.sh
```

Read [install.sh](install.sh) before using it on an existing development system.
It updates Git, installs dependencies, builds and installs IIR, installs the SMI
driver and udev/module configuration, builds the main library and test app,
builds the standalone backend/controller/monitor, and installs the library,
headers, pkg-config file and `cariboulite_util`. The test app and standalone
radio applications remain in `build/`; they are not installed as system commands.
The driver installer regenerates headers under
`software/libcariboulite/src/caribou_smi/kernel/` for the running kernel.

If `SoapySDRUtil` is missing, the script offers to clone, build and install
SoapySDR and SoapyRemote under `installations/`. Accepting is the default.
Declining without an existing `SoapySDRUtil` causes the installer to stop.
If the utility already exists, the script reuses it and does not install
SoapyRemote. Soapy development files must also be discoverable by CMake to
build the CaribouLite plugin; finding the utility alone does not prove that.
For a native-library build without SoapySDR or system installation, use the
[manual userspace build](software/libcariboulite/README.md).

## Boot configuration

The installer checks `/boot/firmware/config.txt` when present, otherwise
`/boot/config.txt` (Ubuntu selects the firmware path). It prints required edits
but does **not** edit that file. Apply these settings in a section that covers
your Pi:

```ini
# Disable the ARM I2C and default SPI configuration lines:
#dtparam=i2c_arm=on
#dtparam=spi=on

# CaribouLite EEPROM and SPI1:
dtparam=i2c_vc=on
dtoverlay=spi1-3cs
```

The script's checks match configuration text, including commented lines;
its warning output is not a full check of the effective boot configuration.
Review the selected file and the settings above. Wi-Fi/Bluetooth disabling
and Trixie locked-memory settings in
[ADDITIONAL-README.md](ADDITIONAL-README.md) describe the recorded setup;
they are separate from the installer checks.

After installation and any boot edits, reboot:

```sh
sudo reboot
```

## Check the installed setup

After reconnecting, run from the repository root:

```sh
tr -d '\000' </proc/device-tree/hat/product
lsmod | grep smi
modinfo -F vermagic smi_stream_dev
ls -l /dev/smi
```

The product should identify `CaribouLite RPI Hat`. Expect `smi_stream_dev` and
`bcm2835_smi`; sizes vary. The module's kernel release must match `uname -r`.
See the [driver guide](driver/README.md) for driver-only installation and
permission details. These checks establish detection and installation state;
they do not demonstrate successful RF streaming.

For interactive diagnostics:

```sh
./build/cariboulite_test_app 2>debug.log
```

Run from the repository root because menu 3 loads `firmware/top.bin` relative
to the working directory. Prepare the
[ALSA Loopback audio route](ADDITIONAL-README.md#creating-the-alsa-loopback-device)
before using the FM audio modes. The separate radio applications have their own
[build, launch and control guide](docs/radio-control.md); close other radio
owners before starting one.

For an installed Soapy plugin, first inspect its installation and discovery:

```sh
SoapySDRUtil --info
SoapySDRUtil --find="driver=Cariboulite"
```

With other radio applications closed, probe the selected device:

```sh
SoapySDRUtil --probe="driver=Cariboulite,channel=HiF"
```

Discovery/probing does not validate a complete Soapy TX or GNU Radio workflow.
Use the [software checks](docs/testing.md) for hardware-free regressions and
the linked physical records for the configurations actually exercised.
