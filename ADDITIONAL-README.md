# Additional CaribouLite notes

Development priorities and acceptance criteria are tracked in the
[development roadmap](ROADMAP.md). The [documentation audit](docs/documentation-audit-2026-09-16.md) records what
was checked and what remains open. These installation notes
combine historical recipes and work in progress; they are not a freshly verified
installation guide. Before applying manual source patches, check whether the
current checkout already contains them. For recorded development state, see the
[software baseline](docs/software-baseline-2026-09-13.md) and
[FPGA study](docs/fpga-gap-study-2026-09-13.md); neither certifies the current setup.

The standalone radio applications are `cariboulite_radio_backend`,
`cariboulite_radio_controller` and `cariboulite_radio_monitor`, built under
`build/radio-backend/`. See [radio controls](docs/radio-control.md) for building
and launching them, including symmetric `rx frequency` / `tx frequency`
commands and grouped squelch controls. The [passive monitor](docs/passive-monitor.md)
remains optional and runs in its own terminal.

# Install CaribouLite with Raspberry pi OS Trixie (64bit) [WIP]
These Trixie notes are a historical workaround and need revalidation against the
current kernel, login/session limits and driver permissions. A Debian-wide policy
change has not been established here. The settings below change system-wide
locked-memory limits; they do not by themselves establish device access or
real-time scheduling permissions.

## Step 1
```bash
sudo mkdir -p /etc/systemd/system.conf.d
```
```bash
sudo tee /etc/systemd/system.conf.d/99-memlock.conf >/dev/null <<'EOF'
[Manager]
DefaultLimitMEMLOCK=infinity
EOF
```
## Step 2
```bash
sudo mkdir -p /etc/systemd/user.conf.d
```
```bash
sudo tee /etc/systemd/user.conf.d/99-memlock.conf >/dev/null <<'EOF'
[Manager]
DefaultLimitMEMLOCK=infinity
EOF
```
## Step 3
```bash
sudo reboot
```
## Step 4
```bash
ulimit -l
cat /proc/self/limits | grep -i "Max locked memory"
```

# Install CaribouLite with Raspberry Pi OS Bookworm (Lite) (64bit)

My goal is to install the cariboulite on a Raspberry Pi0_2W and/or a Pi4. Requirements:
- Connected to the Pi over ethernet and ultimately provide power to the Pi with POE
- Use SDR++ server and SDR++
- Use gnu-radio

The recorded setup fit on a 16 GB SD card; current disk requirements need
remeasuring. SoapySDR is required for Soapy-based applications, while the core
library can build without it. The installer can build SoapySDR when missing.

This recipe was recorded as tested on a 'Pi0' and Pi4 on May 1st 2025.
The exact Zero model needs clarification: a 64-bit recipe does not establish
compatibility with the original Zero/Zero W. Platform validation is DOC-01.

### Initial steps
Download & install the latest version of PiOS using the Raspberry Pi Imager. Ensure that you have enabled ssh under services and that you have provided basic information to the general settings of the OS Customisation.

I make the assumption that all of this is familiar to you and you know how to ssh over the local network to your Pi and know how to work with nano the text editor.

After you have logged on, update the Pi OS:

```
sudo apt update
sudo apt upgrade
```
### Modifying the boot config.txt file
Modify the `/boot/firmware/config.txt` file to ensure the parameters are set so that the cariboulite can communicate with the CPU. The last two lines disable Bluetooth and WiFi, to minimise RF interference. So the Pi needs to be connected to the local network through an ethernet cable, else you can no longer communicate with the Pi after the next reboot.

`sudo nano /boot/firmware/config.txt`

Add the lines below just above the line that reads:
`# Enable audio (loads snd_bcm2835)`

```
dtparam=i2c_arm=off
dtparam=spi=off

# CaribouLite
dtparam=i2c_vc=on
dtoverlay=spi1-3cs

# Disable WiFi and Bluetooth
dtoverlay=disable-wifi
dtoverlay=disable-bt
```

Save the file and reboot

### Install linux kernel header files:

The SMI kernel driver needs headers matching the running kernel. Check:

```sh
uname -r
ls -ld "/lib/modules/$(uname -r)/build"
```

Use the package for your installed kernel and architecture; `linux-headers-rpi-v8`
was the package used for the recorded 64-bit Pi setup. This is a driver-build
requirement, not an SDR++ requirement. See the
[Raspberry Pi kernel documentation](https://www.raspberrypi.com/documentation/computers/linux_kernel.html).

### Monitoring the Pi's activity
The next steps will often take a while, during which you might want to periodically check that everything is still moving along.
In separate terminals watch what is going on during any of the following steps with `htop` and/or `dmesg`. This is how I discovered that `make` ran out of memory.

```
htop
sudo dmesg -wH
```
### Install `git` & `cmake`

```
sudo apt install git cmake
```

# SoapySDR (optional)
If you want to have more control over where SoapySDR en SoapyRemote are installed, execute these steps first. If you use these steps, you should say `No` to the `install SoapySDR` question from the CaribouLite install script.

Create a directory where you want to keep your source code. I call mine: `src`.

```
mkdir -p ~/src
cd ~/src
```

Get the source code:

```
git clone https://github.com/pothosware/SoapySDR.git
git clone https://github.com/pothosware/SoapyRemote.git

```
Or:

```
git clone https://github.com/Habraken/SoapySDR.git
git clone https://github.com/Habraken/SoapyRemote.git
```
The following library not available by default and SoapyRemote will complain but not fail:

```
sudo apt install libavahi-client-dev
```

In both repositories run the below commands to build and install SoapySDR and SoapyRemote starting with SoapySDR as SoapyRemote depends on it.

```
mkdir build && cd build
cmake .. -DENABLE_PYTHON3=ON
make -j`nproc`
sudo make install -j`nproc`
sudo ldconfig #needed on debian systems
SoapySDRUtil --info
```

# CaribouLite
Create a directory where you want to keep your source code. I call mine: `src`.

```
mkdir -p ~/src
cd ~/src
```

Get the source code:

```
git clone https://github.com/cariboulabs/cariboulite.git
```

Or if you don't want to patch yourself:

```
git clone https://github.com/Habraken/cariboulite.git
```

To make the nbfm_tx and nbfm_rx examples work you also need to install the dev tools for ALSA:

```
sudo apt install -y libasound2-dev pkg-config
```

### Current checkout versus historical patches

The current `driver/smi_stream_dev.c` already includes `<linux/vmalloc.h>`,
uses `class_create(DEVICE_NAME)`, and declares `smi_stream_dev_remove` as `void`.
Do not apply the old three-step kernel patch again. NBFM and modem-monitor code
are already in this checkout; switching to `feature/nbfm_rx` is not an
installation prerequisite.

The top-level installer builds the blob generator before the driver and invokes
the driver installer itself; a driver-first installation is unnecessary. It also
runs `git pull`, installs packages and writes system configuration. Its legacy
header/Python package choices still require fresh Bookworm/Trixie validation;
see [DOC-01](ROADMAP.md#documentation-validation-backlog).

Then in the cariboulite directory:

```
./install.sh
```
This takes ‘a while’ on a Pi0. At a certain point the script will ask if you want to `install SoapySDR`, if you did not install it before. Say `Yes`.

After the script has completed, reboot the Pi.
```
sudo reboot
```

Now we need to check whether the SMI driver is working correctly.

```
lsmod | grep smi
```
The ouput should look like this:

```
pi@pi0b-bookworm:~ $ lsmod | grep smi
smi_stream_dev         16384  0
bcm2835_smi            20480  1 smi_stream_dev
pi@pi0b-bookworm:~ $
```
Also check if the permissions are set correclty:

```
ls -l /dev/smi
```
the ouput shoudl look like this:

```
pi@pi0b-bookworm:~ $ ls -l /dev/smi
crw-rw-rw- 1 root root 239, 0 May  1 13:40 /dev/smi
pi@pi0b-bookworm:~ $
```

### Testing SoapySDR

As SoapySDR was install by the install script of cariboulite it should be possible to communicate with the CaribouLite.

```
SoapySDRUtil --find
```
The ouput should look like this:

```
######################################################
##     Soapy SDR -- the SDR abstraction library     ##
######################################################

[INFO] SoapyCaribouliteSession, sessionCount: 0
05-01 13:51:14.339   627   627 I CARIBOU_PROG caribou_prog_configure_from_buffer@caribou_prog.c:260 Sending bitstream of size 32220
05-01 13:51:16.644   627   627 I CARIBOU_PROG caribou_prog_configure_from_buffer@caribou_prog.c:292 FPGA programming - Success!

Printing 'findCariboulite' Request:
Found device 0
  channel = S1G
  device_id = 0
  driver = Cariboulite
  label = CaribouLite S1G[1e3bc7c2]
  name = CaribouLite RPI Hat
  serial = 1e3bc7c2
  uuid = 11884996-87f4-4b07-9961-a38c1b78fa08
  vendor = CaribouLabs LTD
  version = 0x0001

Found device 1
  channel = HiF
  device_id = 1
  driver = Cariboulite
  label = CaribouLite HiF[1e3bc7c3]
  name = CaribouLite RPI Hat
  serial = 1e3bc7c3
  uuid = 11884996-87f4-4b07-9961-a38c1b78fa08
  vendor = CaribouLabs LTD
  version = 0x0001
```

Now it is also possible to interact with the CaribouLite board with the provided `cariboulite_test_app` in the `~/src/cariboulite/build` directory.

```
cd ~/src/cariboulite
./build/cariboulite_test_app
```
At the end of a lot of ouput there should be something like this:

```

	   ____           _ _                 _     _ _
	  / ___|__ _ _ __(_) |__   ___  _   _| |   (_) |_ ___
	 | |   / _` | '__| | '_ \ / _ \| | | | |   | | __/ _ \
	 | |__| (_| | |  | | |_) | (_) | |_| | |___| | ||  __/
	  \____\__,_|_|  |_|_.__/ \___/ \__,_|_____|_|\__\___|


 Select a function:
 [0]  Hard reset FPGA
 [1]  Soft reset FPGA
 [2]  Print board info and versions
 [3]  Program FPGA
 [4]  Perform a Self-Test
 [5]  FPGA Digital I/O
 [6]  FPGA RFFE control
 [7]  FPGA SMI fifo status
 [8]  Modem transmit CW signal
 [9]  Modem receive I/Q stream
 [10]  Synthesizer 85-4200 MHz
 [99]  Quit
    Choice:
```
>Note: If you try option `9` and then select option `2` there will errors that look like this:
>
```
FF D0 FF F4 FF 02 C0 D8  FF C0 FF FC FF C2 FF E6  |  ................
05-01 14:00:45.038   644   648 E CARIBOULITE Radio cariboulite_radio_read_samples@cariboulite_radio.c:1276 SMI data synchronization failed
FF E0 FF D2 C0 12 C0 1A  C0 DA FF 02 C0 00 C0 EE  |  ................
05-01 14:00:45.046   644   648 E CARIBOULITE Radio cariboulite_radio_read_samples@cariboulite_radio.c:1276 SMI data synchronization failed
C0 C0 C0 C4 C0 FA C0 E6  C0 30 C0 22 C0 C0 C0 CC  |  .........0."....
05-01 14:00:45.055   644   648 E CARIBOULITE Radio cariboulite_radio_read_samples@cariboulite_radio.c:1276 SMI data synchronization failed
```
> Historical observation (May 1, 2025): a defective board was suspected. SMI synchronization errors alone do not establish that diagnosis. Board/modem identity, active firmware, driver and stream settings need checking; the hardware investigation remains DOC-08 in the roadmap.

# NBFM and modem monitor
The current menu is defined in `software/libcariboulite/src/app_menu.c`.
Run from the repository root with `./build/cariboulite_test_app 2>debug.log`.
The menu below includes the NBFM and monitor entries; diagnostic screen output
later in this section is a historical capture, not an exact current UI snapshot.

```
	   ____           _ _                 _     _ _
	  / ___|__ _ _ __(_) |__   ___  _   _| |   (_) |_ ___
	 | |   / _` | '__| | '_ \ / _ \| | | | |   | | __/ _ \
	 | |__| (_| | |  | | |_) | (_) | |_| | |___| | ||  __/
	  \____\__,_|_|  |_|_.__/ \___/ \__,_|_____|_|\__\___|


 Select a function:
 [ 0]  Hard reset FPGA
 [ 1]  Soft reset FPGA
 [ 2]  Print board info and versions
 [ 3]  Program FPGA
 [ 4]  Perform a Self-Test
 [ 5]  FPGA Digital I/O
 [ 6]  FPGA RFFE control
 [ 7]  FPGA SMI fifo status
 [ 8]  Modem transmit CW signal
 [ 9]  Modem receive I/Q stream
 [10]  Synthesizer 85-4200 MHz
 [11]  NBFM TX Tone
 [12]  NBFM RX
 [13]  NBFM modem Self-Test
 [14]  Monitor Modem Status
 [99]  Quit
    Choice:

```
Menu option 3 programs `firmware/top.bin` relative to the working directory.
Run the app from the repository root, and distinguish this file from firmware
embedded in a built library. Startup behavior and the image actually running
must be checked rather than inferred from the branch name. The historical
[baseline](docs/software-baseline-2026-09-13.md) records Jan's startup and
programming procedure, with its validation limits.

Options 12 and 14 need configured ALSA playback. In the current option 14
configuration, both TX and RX use RF09 (Sub-1 GHz). TX captures `plughw:Loopback,1,1`, RX plays to
`plughw:Loopback,0,0`, and both default to 430.100 MHz. `tone_mode` defaults to
false; the internal tone parameter is 600 Hz. Therefore pressing T does not
by itself select the historical 650 Hz tone. Audio/device and RF parameters
remain hardcoded (roadmap items 2 and 5). The separate L interface-loopback
diagnostic still uses RF24 reception; T/R normal operation uses RF09.

In option 14, stop TX and RX, then press `1`, `2` or `4` to select a shared
1, 2 or 4 MS/s RF sample rate. The rate applies to both TX and RX, including
NBFM and mono WBFM reception; audio remains at 48 kHz. Rate changes are blocked
while the separate `L` interface-loopback diagnostic is enabled.

Stopping TX sends a 250 ms trailing Quindar tone, followed by silence long
enough to clear the kernel and DMA buffers before switching the radio off.
This takes longer at 1 MS/s; the tone duration stays at 250 ms.

This is how the monitor modem status output should look like:
```
CaribouLite Radio    [T]=TX ON/OFF  [R]=RX ON/OFF  [Q]=QUIT  [X]=RES  1766328674
    TX Loopback: off    TX Frequency: 430100000 Hz    TX Power: -3 d     0.00361
Modem Status Registers:
    RF_CFG:0x08  RF_CLKO:0x1A
    IQIFC0:0x33  IQIFC1:0x11  IQIFC2:0x0B
    RF09-RXFDE :0x81  RF24-RXDFE :0x81
    RF09-TXFDE :0x81  RF24-TXDFE :0x81
    RF09-PADFE :0x40  RF24-PADFE :0x40
    RF09-PAC   :0x6B  RF24-PAC   :0x72
    RF09-IRQM  :0x3F  RF24-IRQM  :0x3F
    RF09-IQRS  :0x00  RF24-IRQS  :0x00
    RF09-STATE :0x02  RF24-STATE :0x02
    RF09-TXDACI:0x7E  RF24-TXDACI:0x00
    RF09-TXDACQ:0x3F  RF24-TXDACQ:0x00
FPGA SMI info (0x05):
    RX FIFO EMPTY: 1
    TX FIFO FULL : 0
    SMI CHANNEL  : 1    // 0=RX09 1=RX24
    SMI DIRECTION: 0    // 0=TX   1=RX
    DEBUG = 0, MODE: 'Low Power (0)'
IQ Data Stream:
    TX_I:0x00000FA1  TX_Q:0x00000013
    RX_I:0x00000000  RX_Q:0x00000000
Linux TX FIFO:
    depth: 64/64 (100%), min:64 max:64
    puts:64 gets:0 drops:0 tO_put:0 tO_get:0
    rate: puts 0.0/s, gets 0.0/s  (expect ~100 fps @ 10ms)
Linux RX FIFO:
    depth: 0/128 (0%), min:128 max:0
    puts:0 gets:0 drops:0 tO_put:0 tO_get:0
    rate: puts 0.0/s, gets 0.0/s  (expect ~100 fps @ 10ms)
```
With `T` you toggle TX and with `R` you toggle RX. Supply audio to the TX loopback endpoint as described in the ALSA section.

>Caution! There is no squelch yet, so when no carrier is present, loud noise is emitted from the speaker or headphones! Reduce your volume before you try this!

# Modifying the firmware (FPGA)

The current build uses Yosys, nextpnr-ice40 and IceStorm's `icepack`;
Arachne-PNR is not used by `firmware/Makefile`. Follow the
[firmware build instructions](firmware/README.md#building-firmware-reliably)
for tool requirements, generated headers, timing checks and failure handling.
The older Ubuntu 14.04/Fedora 24 dependency lists have been retired because they
do not establish a reproducible Bookworm/Trixie toolchain.

For building nextpnr itself, follow its
[upstream instructions](https://github.com/YosysHQ/nextpnr/blob/main/README.md).
Keep the configure and build directories consistent (`cmake -S . -B build ...`
followed by `cmake --build build`). Record tool versions with hardware results.

From the repository root, after the required tools and blob generator exist:

```sh
make -C firmware build
```

This builds the bitstream and generated headers, without programming the board.
Rebuild the userspace library if you need it to embed the new image. To program
the file through menu option 3, run `./build/cariboulite_test_app` from the
repository root. Avoid `make clean` as a routine prerequisite: it removes local
artifacts, including a potentially useful previously validated image.

# SDR++

### Increase the swapfile size (Pi Zero only?)
During the SDR++ build `make` ran out of memory (on the Pi Zero?). Hence I increased the swap file from 512 to 1024.

```
sudo dphys-swapfile swapoff
sudo nano /etc/dphys-swapfile
```
Change the line `CONF_SWAPSIZE=512` to `CONF_SWAPSIZE=1024`. Save the file.

```
sudo dphys-swapfile setup
sudo dphys-swapfile swapon
```

Or:

```
sudo reboot
```

Install dependencies for SDR++:

```
sudo apt install libglfw3 # runtime library
sudo apt install libglfw3-dev
sudo apt install libfftw3-dev
sudo apt install libvolk-dev # confirm package name for your OS
sudo apt install libvolk-bin
sudo apt install libzstd-dev
sudo apt install librtaudio-dev
```

or in one go:

```
sudo apt install libglfw3-dev libfftw3-dev libvolk-dev libvolk-bin libzstd-dev librtaudio-dev

```
### Install via the nightly build package

Use the [upstream SDR++ installation instructions](https://github.com/AlexandreRouma/SDRPlusPlus#installing)
to select an artifact matching the OS and CPU architecture. The previous
hardcoded nightly URL has not been revalidated. Install the downloaded `.deb`
with `sudo apt install ./actual-package-name.deb` from its download directory.
The old recipe used `.db` by mistake and did not save the `curl` output to a file.

### Build from source

Install the development dependencies for the modules you enable. SoapySDR device
discovery (`SoapySDRUtil --find`) is a runtime check, not a prerequisite for
CMake to discover its development libraries. Confirm module availability in the
configure output. The commands below are the recorded source-build recipe;
a fresh build and server configuration check remain DOC-07.

Get the source code:

```
git clone https://github.com/AlexandreRouma/SDRPlusPlus.git
```

```
git clone https://github.com/Habraken/SDRPlusPlus.git
```
In the `~/src/SDRPlusPlus` directory:

```
mkdir build && cd build
```

```
cmake .. -DOPT_BUILD_SOAPY_SOURCE=ON -DOPT_BUILD_HERMES_SOURCE=ON -DOPT_BUILD_SDRPLAY_SOURCE=OFF -DOPT_BUILD_PLUTOSDR_SOURCE=OFF -DOPT_BUILD_AIRSPY_SOURCE=OFF -DOPT_BUILD_AIRSPYHF_SOURCE=OFF -DOPT_BUILD_HACKRF_SOURCE=OFF -DOPT_BUILD_RTL_SDR_SOURCE=OFF
```

```
make -j2
```

```
cd ..
```
```
sh ./create_root.sh
```
```
cd build
```
```
sudo make install
```
```
sudo ldconfig
```


> SDR++ Server and SoapyRemote are separate network interfaces. Local GNU Radio access through the native source or local SoapySDR does not require a SoapyRemote server. A remote Soapy connection requires the corresponding server on the radio host.

### Desktop version SDR++
Before the `soapy_source` is available it needs to be added, in the ‘Module Manager’ of SDR++ if you are running the desktop app on the Pi4. In the 'module manager' select ‘soapy_source’, add a name to the left e.g. ‘Soapy Source’ and click on the tiny plus sign on the right…

Or...

### Headless version SDR++
If you are working from the cli you need to first run SDR++ so that the default config files are created.

```
sdrpp --server
```
Hit ^c to stop the server again.

Edit the `~/.config/sdrpp/config.json` and add the soapy_source module:

```
        ,
        "Soapy Source": {
            "enabled": true,
            "module": "soapy_source"
        }
```

>Note: mind the comma above the double quote!

Save the file and start the SDR++ server.

```
sdrpp --server
```
When the server starts watch the cli output and confirm that the 'Soapy Source' is loaded.

On your remote computer you should now start SDR++ and select SDR++ Server as Source. Provide the correct IP address of the Pi running the SDR++ server. Press `Connect`. At the `Source [REMOTE]` use the drop down menu to select `SoapySDR`. You ay have to press the `Refresh` button first. In the dropdown menu below that make sure you select the device appropriate for your selected frequency.

>Observations: The Pi4 can easily handle 4 MSPS but somehow when the `bandwidth` setting is set to `auto` there is no data coming from the radio. When the bandwidth setting is changed to 2 MHz data is streaming again. However the display bandwidth is 4 MHz!
>The analog bandwidth is only 2.5 MHz according to the datasheet. With `SoapySDRUtil --probe` you'll find a max. sample rate = 4 MSPS and the max. filter bandwidth = 2 MHz.


# Trouble shooting faulty board

### System

```
uname -a
```
```
Linux pi0c-bookworm 6.12.20+rpt-rpi-v8 #1 SMP PREEMPT Debian 1:6.12.20-1+rpt1~bpo12+1 (2025-03-19) aarch64 GNU/Linux
```

### SoapySDRUtil

```
SoapySDRUtil --find
```

```
######################################################
##     Soapy SDR -- the SDR abstraction library     ##
######################################################

[INFO] SoapyCaribouliteSession, sessionCount: 0
05-02 07:55:11.142   644   644 I FPGA caribou_fpga_program_to_fpga@caribou_fpga.c:210 FPGA already operational - not programming (use 'force_prog=true' to force update)
Printing 'findCariboulite' Request:
Found device 0
  channel = S1G
  device_id = 0
  driver = Cariboulite
  label = CaribouLite S1G[1e3bc7c2]
  name = CaribouLite RPI Hat
  serial = 1e3bc7c2
  uuid = 11884996-87f4-4b07-9961-a38c1b78fa08
  vendor = CaribouLabs LTD
  version = 0x0001

Found device 1
  channel = HiF
  device_id = 1
  driver = Cariboulite
  label = CaribouLite HiF[1e3bc7c3]
  name = CaribouLite RPI Hat
  serial = 1e3bc7c3
  uuid = 11884996-87f4-4b07-9961-a38c1b78fa08
  vendor = CaribouLabs LTD
  version = 0x0001
```

```
SoapySDRUtil --probe
```

```
######################################################
##     Soapy SDR -- the SDR abstraction library     ##
######################################################

Probe device
[INFO] SoapyCaribouliteSession, sessionCount: 0
05-02 08:03:21.048   712   712 I FPGA caribou_fpga_program_to_fpga@caribou_fpga.c:210 FPGA already operational - not programming (use 'force_prog=true' to force update)
Printing 'findCariboulite' Request:
[INFO] Initializing DeviceID: 0, Label: CaribouLite S1G[1e3bc7c2], ChannelType: S1G
[INFO] Creating SampleQueue MTU: 131072 I/Q samples (524288 bytes)

----------------------------------------------------
-- Device identification
----------------------------------------------------
  driver=Cariboulite
  hardware=Cariboulite Rev2.8
  device_id=0
  fpga_revision=1
  hardware_revision=0x0001
  product_name=CaribouLite RPI Hat
  serial_number=253617121
  vendor_name=CaribouLabs LTD

----------------------------------------------------
-- Peripheral summary
----------------------------------------------------
  Channels: 1 Rx, 1 Tx
  Timestamps: NO

----------------------------------------------------
-- RX Channel 0
----------------------------------------------------
  Full-duplex: NO
  Supports AGC: YES
  Stream formats: CS16, CS8, CF32, CF64
  Native format: CS16 [full-scale=4095]
  Antennas: TX/RX Sub1GHz
  Full gain range: [0, 69] dB
    Modem AGC gain range: [0, 69] dB
  Full freq range: [389.5, 510], [779, 1020] MHz
    RF freq range: [389.5, 510], [779, 1020] MHz
  Sample rates: 4, 2, 1.33333, 1, 0.8, 0.666667, 0.5, 0.4 MSps
  Filter bandwidths: 0.02, 0.05, 0.1, 0.16, 0.2, 0.8, 1, 1.25, 1.6, 2 MHz
  Sensors: RSSI, ENERGY, PLL_LOCK_MODEM
     * RSSI (RX RSSI):[-127, 4] 0.000000
        Modem level RSSI measurment
     * ENERGY (RX ENERGY):[-127, 4] 0.000000
        Modem level ENERGY (EDC) measurment
     * PLL_LOCK_MODEM (PLL Lock Modem): 1.000000
        Modem PLL locking indication

----------------------------------------------------
-- TX Channel 0
----------------------------------------------------
  Full-duplex: NO
  Supports AGC: NO
  Stream formats: CS16, CS8, CF32, CF64
  Native format: CS16 [full-scale=4095]
  Antennas: TX/RX Sub1GHz
  Full gain range: [0, 31] dB
    Modem PA gain range: [0, 31] dB
  Full freq range: [389.5, 510], [779, 1020] MHz
    RF freq range: [389.5, 510], [779, 1020] MHz
  Sample rates: 4, 2, 1.33333, 1, 0.8, 0.666667, 0.5, 0.4 MSps
  Filter bandwidths: 0.08, 0.1, 0.125, 0.16, 0.2, 0.4, 0.5, 0.625, 0.8, 1 MHz
  Sensors: PLL_LOCK_MODEM
     * PLL_LOCK_MODEM (PLL Lock Modem): 1.000000
        Modem PLL locking indication
```

### Self test

Error messages during the selftest using the `cariboulite_test_app`:

```
05-02 07:38:08.605   622   622 D CARIBOULITE Setup
cariboulite_self_test@cariboulite_setup.c:480 Testing modem communication and versions
05-02 07:38:08.606   622   622 W AT86RF215_Main
at86rf215_print_version@at86rf215.c:294 MODEM Version: not AT86RF215 IQ capable modem (product number: 0x0d, versions 03)
05-02 07:38:08.606   622   622 E CARIBOULITE Setup
cariboulite_self_test@cariboulite_setup.c:486 The assembled modem is not AT86RF215 / IQ variant (product number: 0x0d)
05-02 07:38:08.606   622   622 D CARIBOULITE Setup
cariboulite_self_test@cariboulite_setup.c:495 Testing mixer communication and versions
05-02 07:38:08.608   622   622 E CARIBOULITE Setup
cariboulite_self_test@cariboulite_setup.c:513 Self-test process finished with errors
```

I have also tried all the other options such as hard reset of the fpga, soft reset of the fpga, reprogramming of the fpga. During the receive test there are always smi sync errors.

# ALSA

ALSA is complicated. To have some flexibility for testing the nbfm modulator and demodulator it can be helpfull to not have to depend on a physical audio device, such as a sound card.This can be done with a ALSA utility: snd_aloop. What I describe here is based on this article:

based on: https://linuxvox.com/blog/linux-without-hardware-soundcard-capture-audio-playback-and-record-it-to-file/

### Prerequisites:

Ensure
```alsa-utils```  ```ffmpeg``` ```sox```
are installed.

note :both alsa-utils and ffmpeg, are already part of the Raspberry OS usualy.

### Making the Loopback Module Persistent

The modprobe command loads the module temporarily (it will unload on reboot). To make it persistent:

Create a configuration file in /etc/modules-load.d/ to load the module at boot:

sudo nano /etc/modules-load.d/alsa-loopback.conf
Add the following line and save the file:

```snd-aloop```
Reboot to test persistence (optional, but recommended):

sudo reboot
After reboot, re-run lsmod | grep snd_aloop to confirm the module is loaded.

### Long-running audio bridges

Use `-t raw` on **both** `arecord` and `aplay` when piping audio between
ALSA devices. Without it, `arecord` defaults to WAV and limits its piped
output to 2 GiB. At 48 kHz, mono, 16-bit audio, that is about **6 hours
13 minutes**; the bridge can then exit and return to the shell prompt.
Raw PCM avoids this WAV size limit. See the
[ALSA capture implementation](https://github.com/alsa-project/alsa-utils/blob/master/aplay/aplay.c).

The commands below use `plughw:CARD=USB,DEV=0`, the Jabra's card ID on this
setup. Check `arecord -l` and `aplay -l` and substitute your device's ID if
needed. Card IDs avoid relying on the numeric card index `4` staying the same.
Run each bridge in its own terminal; stop it with Ctrl+C. Only the RX bridge
is needed for listening to NBFM or mono WBFM. The TX bridge supplies microphone
audio when transmitting.

With these commands, startup should report `Recording raw data` and
`Playing raw data`. Raw mode removes the container size limit; it does not
prevent buffer underruns or overruns. The September 19, 2026 bridge exits
were consistent with the WAV limit; extended testing with raw mode is ongoing.

### Testing TX

To test if the ALSA Loopback device works you can start the cariboulite_test_app, enable tx and in a separate terminal run the following command: ```speaker-test -D plughw:Loopback,0,1 -c 1 -t sine -f 440 -r 48000```. You should now here a 440 Hz tone on a narrow band fm receiver tuned to the correct frequency.

To route the USB microphone to the application's TX capture endpoint:

```bash
arecord -D plughw:CARD=USB,DEV=0 -t raw -f S16_LE -c 1 -r 48000 |
  aplay -D plughw:Loopback,0,1 -t raw -f S16_LE -c 1 -r 48000
```

For some obscure reason the alsaloop command doesn't work for this route.

### Testing RX

To route the application's received audio from Loopback to the USB speaker:

```bash
arecord -D plughw:Loopback,1,0 -t raw -f S16_LE -c 1 -r 48000 |
  aplay -D plughw:CARD=USB,DEV=0 -t raw -f S16_LE -c 1 -r 48000
```

If faced with ```overruns!!!``` one could also use:

```alsaloop -C plughw:Loopback,1,0 -P plughw:4,0 -r 48000 -c 1 -f S16_LE -t 50000```

or

```bash
arecord -D plughw:Loopback,1,0 -t raw -f S16_LE -c 1 -r 48000 --buffer-time=100000 --period-time=25000 |
  aplay -D plughw:CARD=USB,DEV=0 -t raw -f S16_LE -c 1 -r 48000 --buffer-time=100000 --period-time=25000
```

To route RX audio to the PI4's headphone jack:

```bash
arecord -D plughw:Loopback,1,0 -t raw -f S16_LE -c 1 -r 48000 |
  aplay -D plughw:CARD=Headphones,DEV=0 -t raw -f S16_LE -c 1 -r 48000
```

# New developments
We have implemented a new application that is split in three modules that are run in separate terminals:


This creates these launchers under build/radio-backend/:
- cariboulite_radio_backend
- cariboulite_radio_controller
- cariboulite_radio_monitor

They currently run from that directory; there’s no system installation target.

he standalone apps use a separate Makefile. After the main build, run from the repository root:

```
make -C software/libcariboulite/tools/radio_backend
```


# GNU-RADIO (WIP)
```
sudo apt install gnuradio
```


Installing GNU Radio alone does not install this repository's native block.
`software/gr-caribouLite/CMakeLists.txt` requests GNU Radio 3.10 and its `lib/`
subdirectory looks up installed `cariboulite` through pkg-config. The tree has
`caribouLiteSource_impl.cc` and a source GRC definition, but no native TX sink.
SoapySDR has a separate TX/RX adapter. Working flowgraphs, current dependency
validation and TX/RX hardware checks remain under roadmap item 9 and DOC-07.
