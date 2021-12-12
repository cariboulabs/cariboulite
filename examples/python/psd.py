from PySimpleGUI.PySimpleGUI import Canvas, Column
import time
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
import SoapySDR
from SoapySDR import SOAPY_SDR_RX, SOAPY_SDR_TX, SOAPY_SDR_CS16
from PySimpleGUI import Window, WIN_CLOSED, Slider, Button, theme, Text, Radio, Image, InputText, Canvas
from numpy.lib.arraypad import pad


def setup_receiver(sdr, channel, freq_hz):
    use_agc = False                                             # Use or don't use the AGC
    # The wide channel parameters
    sdr.setGainMode(SOAPY_SDR_RX, channel, use_agc)             # Set the gain mode
    sdr.setGain(SOAPY_SDR_RX, channel, 50)                      # Set the gain
    sdr.setFrequency(SOAPY_SDR_RX, channel, freq_hz)            # Tune the LO
    sdr.setBandwidth(SOAPY_SDR_RX, channel, 2.5e6)
    rx_stream = sdr.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16, [channel])  # Setup data stream
    return rx_stream

def update_receiver_freq(sdr, stream, channel, freq_hz):
    sdr.setFrequency(SOAPY_SDR_RX, channel, freq_hz)


# Data and Source Configuration
rx_chan = 1                 # 6G = 1
N = 16384                   # Number of complex samples per transfer
rx_buff = np.empty(2 * N, np.int16)  # Create memory buffer for data stream
freq = 915.6e6

#  Initialize CaribouLite Soapy
sdr = SoapySDR.Device(dict(driver="Cariboulite"))         # Create Cariboulite
rx_stream = setup_receiver(sdr, rx_chan, freq)
sdr.activateStream(rx_stream)

sr = sdr.readStream(rx_stream, [rx_buff], N, timeoutUs=int(5e6))
# Make sure that the proper number of samples was read
rc = sr.ret
if (rc != N):
    print("Error Reading Samples from Device (error code = %d)!" % rc)
    exit

s_real = rx_buff[::2].astype(np.float32)
s_imag = -rx_buff[1::2].astype(np.float32)
x = s_real + 1j*s_imag

## PSD
Fs = 4e6
#N = 2048
#x = x[0:N] # we will only take the FFT of the first 1024 samples, see text below
x = x * np.hamming(len(x)) # apply a Hamming window

PSD = (np.abs(np.fft.fft(x))/N)**2
PSD_log = 10.0*np.log10(PSD)
PSD_shifted = np.fft.fftshift(PSD_log)

center_freq = freq
f = np.arange(Fs/-2.0, Fs/2.0, Fs/N) # start, stop, step.  centered around 0 Hz
f += center_freq # now add center frequency

fig = plt.figure()
plt.plot(f, PSD_shifted)
plt.show()

fig = plt.figure()
plt.plot(s_real)
plt.plot(s_imag)
plt.show()
