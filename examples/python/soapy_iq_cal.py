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

##
## WINDOW FUNCTIONS
def draw_figure_with_toolbar(canvas, fig, canvas_toolbar):
    if canvas.children:
        for child in canvas.winfo_children():
            child.destroy()
    if canvas_toolbar.children:
        for child in canvas_toolbar.winfo_children():
            child.destroy()
    
    figure_canvas_agg = FigureCanvasTkAgg(fig, master=canvas)
    figure_canvas_agg.draw()

    toolbar = Toolbar(figure_canvas_agg, canvas_toolbar)
    toolbar.update()
    figure_canvas_agg.get_tk_widget().pack(side='right', fill='both', expand=1)


class Toolbar(NavigationToolbar2Tk):
    def __init__(self, *args, **kwargs):
        super(Toolbar, self).__init__(*args,**kwargs)


def create_window():
    #theme("Default")
    layout = [
        [Column(
            layout=[
                [Text('Receiver LO [Hz]:'), InputText('868000000', key='RxFreq'), Text('RX Bandwidth: 2.5 MHz')],
                [Text('Transmitter Fc [Hz]:'), Text(key='TxCWFreq')],
            ]
        )],
        [Canvas(key='controls_cv')],
        [Column(
            layout=[
                [Canvas(key='fig_cv', size=(300*3, 300))]
            ],
            background_color='#DAE0E6',
            pad=(0,0)
        )],
        [Text('RSSI:', key='rssi'), Text('Alpha:', key='alpha'), Text('Cos(Phi):', key='cos_phi')],
        [Canvas(key='controls_params_cv')],
        [Column(
            layout=[
                [Canvas(key='params_cv', size=(200*4, 200))]
            ],
            background_color='#DAE0E6',
            pad=(0,0)
        )],
        [Button("Correct", size=(10,1)), Button("Exit", size=(10,1)), Button("Run", size=(10,1))],
    ]
    window = Window("CaribouLite PlayGround", layout, location=(800,400))
    return window

def calculate_psd(I,Q, fs):
    x = I + 1j*Q
    N = 16384  # len(I)
    x = x * np.hamming(len(x)) # apply a Hamming window
    PSD = (np.abs(np.fft.fft(x))/N)**2
    PSD_log = 10.0*np.log10(PSD)
    PSD_shifted = np.fft.fftshift(PSD_log)
    f = np.arange(fs/-2.0, fs/2.0, fs/N) # start, stop, step.  centered around 0 Hz
    return f,PSD_shifted

def update_iq_graphs(window, I,Q,I_new, Q_new):
    plt.figure(1).clf()
    fig = plt.gcf()
    DPI = fig.get_dpi()
    fig.set_size_inches(304*4/float(DPI), 304/float(DPI))
    axs = fig.add_subplot(131)
    plt.scatter(I, Q)
    axs.set_aspect('equal', adjustable='box')
    plt.title('I/Q original')

    axs = fig.add_subplot(132)
    plt.scatter(I_new, Q_new)
    axs.set_aspect('equal', adjustable='box')
    plt.title('I/Q corrected')

    f, psd = calculate_psd(I,Q, 4e6)
    f_corrected, psd_corrected = calculate_psd(I_new,Q_new, 4e6)

    plt.subplot(133)
    plt.plot(f, psd)
    plt.plot(f_corrected, psd_corrected)

    plt.grid()
    draw_figure_with_toolbar(window['fig_cv'].TKCanvas, fig, window['controls_cv'].TKCanvas)

def update_est_graphs(window, freq_diff, g_vec, phi_vec, rssi_vec):
    plt.figure(2).clf()
    fig = plt.gcf()
    DPI = fig.get_dpi()
    fig.set_size_inches(204*3/float(DPI), 204/float(DPI))
    plt.subplot(131)
    plt.plot(freq_diff, g_vec)
    plt.title('G')
    plt.subplot(132)
    plt.plot(freq_diff, phi_vec)
    plt.title('phi')
    plt.subplot(133)
    plt.plot(freq_diff, rssi_vec)
    plt.title('Relative Power')
    plt.grid()
    draw_figure_with_toolbar(window['params_cv'].TKCanvas, fig, window['controls_params_cv'].TKCanvas)

##
## I/Q Correction Function - Blind correction

def fix_iq_blind(x):
    z = x - np.mean(x)
    p_in = np.var(z)

    theta1 = (-1) * np.mean(np.sign(z.real) * z.imag)
    theta2 = np.mean(np.abs(z.real))
    theta3 = np.mean(np.abs(z.imag))
    c1 = theta1/theta2
    c2 = np.sqrt( ( (theta3**2 - theta1**2) / theta2**2 ) )

    g = theta3/theta2
    phi = np.arcsin(theta1/theta3)
    wI = c2 * z.real
    wQ = c1 * z.real + z.imag
    z_out = wI + 1j * wQ

    return (z_out, g, phi, p_in, c1, c2)

##
## I/Q Correction Function - single tone correction

def fix_iq_imbalance(x):
    # remove DC and save input power
    z = x - np.mean(x)
    p_in = np.var(z)

    # scale Q to have unit amplitude (remember we're assuming a single input tone)
    Q_amp = np.sqrt(2*np.mean(z.imag**2))       # <== should be z.imag**2????
    z /= Q_amp

    I, Q = z.real, z.imag

    alpha_est = np.sqrt(2*np.mean(I**2))
    sin_phi_est = (2/alpha_est)*np.mean(I*Q)
    cos_phi_est = np.sqrt(1 - sin_phi_est**2)

    I_new_p = (1/alpha_est)*I
    Q_new_p = (-sin_phi_est/alpha_est)*I + Q

    y = (I_new_p + 1j*Q_new_p)/cos_phi_est

    print ('phase error:', np.arccos(cos_phi_est)*180/np.pi, 'degrees')
    print ('amplitude error:', 20*np.log10(alpha_est), 'dB')
    z_out = y*np.sqrt(p_in/np.var(y))
    return (z_out, alpha_est, np.arccos(cos_phi_est), p_in, sin_phi_est, cos_phi_est)


## 
## Soapy Control functions

def setup_receiver(sdr, channel, freq_hz):
    use_agc = False                                             # Use or don't use the AGC
    # The wide channel parameters
    sdr.setGainMode(SOAPY_SDR_RX, channel, use_agc)             # Set the gain mode
    sdr.setGain(SOAPY_SDR_RX, channel, 70)                      # Set the gain
    sdr.setFrequency(SOAPY_SDR_RX, channel, freq_hz)            # Tune the LO
    sdr.setBandwidth(SOAPY_SDR_RX, channel, 1.6e6)
    rx_stream = sdr.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16, [channel])  # Setup data stream
    return rx_stream

def update_receiver_freq(sdr, stream, channel, freq_hz):
    sdr.setFrequency(SOAPY_SDR_RX, channel, freq_hz)

def setup_transmitter(sdr, channel, freq_hz):
    sdr.setFrequency(SOAPY_SDR_TX, channel, 867.5e6)
    sdr.setGain(SOAPY_SDR_TX, channel, 60)
    tx_stream = sdr.setupStream(SOAPY_SDR_TX, SOAPY_SDR_CS16, [channel], dict(CW="1"))
    return tx_stream

def update_transmitter_freq(sdr, stream, channel, freq_hz):
    sdr.deactivateStream(stream)
    sdr.setFrequency(SOAPY_SDR_TX, channel, freq_hz)
    sdr.activateStream(stream)


##
## MAIN

def main():
    # Data and Source Configuration
    tx_chan = 0                 # S1G = 0
    rx_chan = 1                 # 6G = 1
    N = 16384                   # Number of complex samples per transfer
    rx_buff = np.empty(2 * N, np.int16)  # Create memory buffer for data stream

    #  Initialize CaribouLite Soapy
    sdr = SoapySDR.Device(dict(driver="Cariboulite"))         # Create Cariboulite
    rx_stream = setup_receiver(sdr, rx_chan, 868e6)
    tx_stream = setup_transmitter(sdr, tx_chan, 868.5e6)
    sdr.activateStream(tx_stream)
    sdr.activateStream(rx_stream)

    # create the window
    window = create_window()
    while True:
        event, values = window.read(timeout = 20)

        if (event == "Exit" or event == WIN_CLOSED):
            break;
        elif event == "Correct":
            print("clicked correct")

            freq = float(values['RxFreq'])
            tx_freq = freq + 0.5e6
            print("setting frequency ", freq)
            window['TxCWFreq'].update(tx_freq)
            
            update_transmitter_freq(sdr, tx_stream, tx_chan, tx_freq)
            update_receiver_freq(sdr, rx_stream, rx_chan, freq)
            time.sleep(0.5)

            sr = sdr.readStream(rx_stream, [rx_buff], N, timeoutUs=int(5e6))
            # Make sure that the proper number of samples was read
            rc = sr.ret
            if (rc != N):
                print("Error Reading Samples from Device (error code = %d)!" % rc)
                break;

            s_real = rx_buff[::2].astype(np.float32)
            s_imag = rx_buff[1::2].astype(np.float32)
            z = s_real + 1j*s_imag
            (z_out, g, phi, p_in,a,b) = fix_iq_imbalance(z)
            #(z_out, g, phi, p_in,a,b) = fix_iq_blind(z)
            rssi = 20*np.log10(p_in)
            phi *= 180/np.pi

            window['rssi'].update('RSSI: %f dBm' % rssi)
            window['alpha'].update('Alpha: %f' % g)
            window['cos_phi'].update('Phi: %f' % phi)

            update_iq_graphs(window, s_real, s_imag, z_out.real, z_out.imag)

        elif event == "Run":
            print("clicked run")
            bw = 2.5e6
            rx_freq = float(values['RxFreq'])
            update_receiver_freq(sdr, rx_stream, rx_chan, rx_freq)

            freq_num = 60
            tx_freq_vec = rx_freq + np.linspace(-bw/2, bw/2, freq_num)
            g_est_vec = np.empty(tx_freq_vec.size, np.float32) 
            phi_vec = np.empty(tx_freq_vec.size, np.float32)
            rssi_vec = np.empty(tx_freq_vec.size, np.float32)
            
            index = 0
            for tx_freq in tx_freq_vec:
                window['TxCWFreq'].update(tx_freq)
                update_transmitter_freq(sdr, tx_stream, tx_chan, tx_freq)    
                time.sleep(1)

                sr = sdr.readStream(rx_stream, [rx_buff], N, timeoutUs=int(5e6))
                # Make sure that the proper number of samples was read
                rc = sr.ret
                if (rc != N):
                    print("Error Reading Samples from Device (error code = %d)!" % rc)
                    break;

                s_real = rx_buff[::2].astype(np.float32)
                s_imag = rx_buff[1::2].astype(np.float32)
                z = s_real + 1j*s_imag
                (z_out, g, phi, p_in, a,b) = fix_iq_imbalance(z)
                #(z_out, g, phi, p_in, a,b) = fix_iq_blind(z)

                g_est_vec[index] = 20*np.log10(g)
                phi_vec[index] = phi*180/np.pi
                rssi_vec[index] = 20*np.log10(p_in)

                window['rssi'].update('RSSI: %f dBm' % rssi_vec[index])
                window['alpha'].update('Alpha: %f' % g_est_vec[index])
                window['cos_phi'].update('Phi: %f' % phi_vec[index])

                update_iq_graphs(window, s_real, s_imag, z_out.real, z_out.imag)
                index += 1
                time.sleep(0.1)
            
            update_est_graphs(window, tx_freq_vec-rx_freq, g_est_vec, phi_vec, rssi_vec - np.max(rssi_vec))


    # Stop streaming and close the connection to the radio
    window.close()
    sdr.deactivateStream(rx_stream)
    sdr.deactivateStream(tx_stream)
    sdr.closeStream(rx_stream)
    sdr.closeStream(tx_stream)



###########################################################
## Global Area

# Enumerate the Soapy devices
res = SoapySDR.Device.enumerate()
for result in res:
    print(result)

# run the program
main()
