#!/usr/bin/python3

import matplotlib.pyplot as plt
import scipy
import numpy as np

# Plots the impulse response of the Bandstop and its frequency response

def plot_if(figno,name,figtitle):
    plt.figure(figno)
    plt.suptitle(figtitle)
    fs = 1000
    y = np.loadtxt(name) / 1000
    y2 = y - np.mean(y)
    plt.subplot(211)
    plt.title("Signal")
    plt.ylim(-1,1)
    plt.plot(y2);
    #
    # Fourier Transform
    yf = np.fft.fft(y) / len(y)
    plt.subplot(212)
    plt.plot(np.linspace(0,fs,len(yf)),20*np.log10(np.abs(yf)))
    plt.xlim(0,fs/2)
    plt.ylim(-130,-10)
    plt.title("Frequency spectrum")
    plt.xlabel("f/Hz")
    plt.ylabel("amplitude/dB")

plot_if(1,"ecg50hz.dat","ECG with 50Hz noise")

plot_if(2,"ecg_filtered.dat","ECG after filtering")

plt.show()
