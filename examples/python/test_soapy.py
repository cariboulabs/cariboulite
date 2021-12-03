import numpy as np
import matplotlib.pyplot as plt
import SoapySDR
from SoapySDR import SOAPY_SDR_RX, SOAPY_SDR_CS16

# Enumerate the Soapy devices
res = SoapySDR.Device.enumerate()
for result in res:
    print(result)

# Data and Source Configuration
rx_chan = 1                 # RX1 = 0, RX2 = 1
N = 16384                   # Number of complex samples per transfer
fs = 4e6                    # Radio sample Rate
freq = 868e6                # LO tuning frequency in Hz
use_agc = True              # Use or don't use the AGC
timeout_us = int(5e6)

#  Initialize CaribouLite
sdr = SoapySDR.Device(dict(driver="Cariboulite"))         # Create Cariboulite
sdr.setSampleRate(SOAPY_SDR_RX, rx_chan, fs)              # Set sample rate
sdr.setGainMode(SOAPY_SDR_RX, rx_chan, use_agc)           # Set the gain mode
sdr.setFrequency(SOAPY_SDR_RX, rx_chan, freq)             # Tune the LO
sdr.setBandwidth(SOAPY_SDR_RX, rx_chan, 2.5e6)

# Create data buffer and start streaming samples to it
rx_buff = np.empty(2 * N, np.int16)  # Create memory buffer for data stream
rx_stream = sdr.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16, [
                            rx_chan])  # Setup data stream
sdr.activateStream(rx_stream)  # this turns the radio on

# Save the file names for plotting later. Remove if not plotting.
file_names = []
file_ctr = 0

nfiles = 1

while file_ctr < nfiles:
    print(file_ctr, '. READING ', N, ' samples from CaribouLite')
    # Read the samples from the data buffer
    sr = sdr.readStream(rx_stream, [rx_buff], N, timeoutUs=timeout_us)

    # Make sure that the proper number of samples was read
    rc = sr.ret
    assert rc == N, 'Error Reading Samples from Device (error code = %d)!' % rc

    s_real = rx_buff[::2].astype(np.float32)
    s_imag = rx_buff[1::2].astype(np.float32)

    c = s_real + 1j * s_imag
    c_no_bias = c - c.mean()

    # scale Q to have unit amplitude
    Q_amp = np.sqrt(2*np.mean(s_imag**2))
    c /= Q_amp
    I, Q = c.real, c.imag

    alpha_est = np.sqrt(2*np.mean(I**2))
    sin_phi_est = (2/alpha_est)*np.mean(I*Q)
    cos_phi_est = np.sqrt(1-sin_phi_est**2)

    I_new_p = (1/alpha_est) * I
    Q_new_p = (-sin_phi_est/alpha_est) * I + Q
    y = (I_new_p + 1j*Q_new_p) / cos_phi_est

    print('phase error: ', np.arccos(cos_phi_est)*360/3.1415, ' degrees')
    print('amplitude error: ', 20*np.log10(alpha_est), ' dB' )

    # plt.plot(s_real, s_imag, 'k', label='I')
    plt.scatter(I_new_p, Q_new_p)
    plt.ylabel('I / Q Balance')
    plt.show()

    file_ctr += 1
    if file_ctr > nfiles:
        break

# Stop streaming and close the connection to the radio
sdr.deactivateStream(rx_stream)
sdr.closeStream(rx_stream)
