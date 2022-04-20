# PySimpleGUI
from PySimpleGUI.PySimpleGUI import Canvas, Column
from PySimpleGUI import Window, WIN_CLOSED, Slider, Button, theme, Text, Radio, Image, InputText, Canvas

# Numpy
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
import numpy as np
from numpy.lib.arraypad import pad

# System
import time

# Soapy
import SoapySDR
from SoapySDR import SOAPY_SDR_RX, SOAPY_SDR_TX, SOAPY_SDR_CS16


def setup_receiver(sdr, freq_hz):
	use_agc = False
	sdr.setGainMode(SOAPY_SDR_RX, 0, use_agc)
	sdr.setGain(SOAPY_SDR_RX, 0, 50)
	sdr.setBandwidth(SOAPY_SDR_RX, 0, 2500e5)
	sdr.setFrequency(SOAPY_SDR_RX, 0, freq_hz)
	rx_stream = sdr.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16, [0])
	return rx_stream


def update_receiver_freq(sdr, stream, channel, freq_hz):
    sdr.setFrequency(SOAPY_SDR_RX, channel, freq_hz)


##
## GLOBAL AREA
##

# Data and Source Configuration
Fs = 4e6
N = int(Fs/4)                   # Number of complex samples per transfer
rx_buff = np.empty(2 * N, np.int16)  # Create memory buffer for data stream
freq = 1090e6

#  Initialize CaribouLite Soapy
sdr = SoapySDR.Device({"driver": "Cariboulite", "device_id": "1"})
rx_stream = setup_receiver(sdr, freq)
sdr.activateStream(rx_stream)

sr = sdr.readStream(rx_stream, [rx_buff], N, timeoutUs=int(5e6))
# Make sure that the proper number of samples was read
rc = sr.ret
if (rc != N):
    print("Error Reading Samples from Device (error code = %d)!" % rc)
    exit

s_real = rx_buff[::2].astype(np.float32) / 4096.0
s_imag = rx_buff[1::2].astype(np.float32) / 4096.0
x = s_real + 1j*s_imag

## PSD

#N = 2048
#x = x[0:N] # we will only take the FFT of the first 1024 samples, see text below
#x = x * np.hamming(len(x)) # apply a Hamming window

#PSD = (np.abs(np.fft.fft(x))/N)**2
#PSD_log = 10.0*np.log10(PSD)
#PSD_shifted = np.fft.fftshift(PSD_log)

#center_freq = freq
#f = np.arange(Fs/-2.0, Fs/2.0, Fs/N) # start, stop, step.  centered around 0 Hz
#f += center_freq # now add center frequency

window_size = 2048
overlap = window_size - 64

fig = plt.figure()
#plt.plot(f, PSD_shifted)
plt.specgram(x, NFFT=window_size, Fs=Fs, noverlap=overlap, mode='psd')
plt.show()

#fig = plt.figure()
#plt.plot(s_real)
#plt.plot(s_imag)
#plt.show()