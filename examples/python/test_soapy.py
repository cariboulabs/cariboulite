from PySimpleGUI.PySimpleGUI import Canvas, Column
import numpy as np
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
import SoapySDR
from SoapySDR import SOAPY_SDR_RX, SOAPY_SDR_TX, SOAPY_SDR_CS16
from PySimpleGUI import Window, WIN_CLOSED, Slider, Button, theme, Text, Radio, Image
from numpy.lib.arraypad import pad

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
                [Text('Receiver LO')],
                [Text('Transmitter Fc')]
            ]
        )]
        [Canvas(key='controls_cv')],
        [Column(
            layout=[
                [Canvas(key='fig_cv', size=(400*2, 400))]
            ],
            background_color='#DAE0E6',
            pad=(0,0)
        )],
        [Button("Correct", size=(10,1)), Button("Exit", size=(10,1))],
    ]
    window = Window("CaribouLite PlayGround", layout, location=(800,400))
    return window


def estimate_correct(rx_buff):
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

    print('phase error: ', np.arccos(cos_phi_est)*360/3.1415, ' degrees')
    print('amplitude error: ', 20*np.log10(alpha_est), ' dB' )

    I_new_p = (1/alpha_est) * I
    Q_new_p = (-sin_phi_est/alpha_est) * I + Q
    y = (I_new_p + 1j*Q_new_p) / cos_phi_est

    return (s_real, s_imag, alpha_est, sin_phi_est, cos_phi_est, I_new_p, Q_new_p)

def main():
    # Data and Source Configuration
    tx_chan = 0                 # S1G = 0, 6G = 1
    rx_chan = 1                 # S1G = 0, 6G = 1
    N = 16384                   # Nu5mber of complex samples per transfer
    fs = 4e6                    # Radio sample Rate
    freq = 868e6                # LO tuning frequency in Hz
    use_agc = True              # Use or don't use the AGC
    gain = 60
    n_reads = 0

    rx_buff = np.empty(2 * N, np.int16)  # Create memory buffer for data stream
    timeout_us = int(5e6)

    #  Initialize CaribouLite
    sdr = SoapySDR.Device(dict(driver="Cariboulite"))         # Create Cariboulite

    # The wide channel parameters
    sdr.setSampleRate(SOAPY_SDR_RX, rx_chan, fs)              # Set sample rate
    sdr.setGainMode(SOAPY_SDR_RX, rx_chan, use_agc)           # Set the gain mode
    sdr.setGain(SOAPY_SDR_RX, rx_chan, gain)                  # Set the gain
    sdr.setFrequency(SOAPY_SDR_RX, rx_chan, freq)             # Tune the LO
    sdr.setBandwidth(SOAPY_SDR_RX, rx_chan, 2.5e6)

    # The S1G channel parameter
    sdr.setFrequency(SOAPY_SDR_TX, tx_chan, 868.5e6)
    sdr.setGain(SOAPY_SDR_TX, tx_chan, 50)
    cw_tx_stream = sdr.setupStream(SOAPY_SDR_TX, SOAPY_SDR_CS16, [tx_chan], dict(CW="1"))
    sdr.activateStream(cw_tx_stream)

    # Create data buffer and start streaming samples to it
    rx_stream = sdr.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16, 
                                [rx_chan])  # Setup data stream
    sdr.activateStream(rx_stream)  # this turns the radio on

    # create the window
    window = create_window()

    while True:
        event, values = window.read(timeout = 20)

        if (event == "Exit" or event == WIN_CLOSED):
            break;

        if (event == "Correct"):
            n_reads += 1
            sr = sdr.readStream(rx_stream, [rx_buff], N, timeoutUs=timeout_us)
            
            # Make sure that the proper number of samples was read
            rc = sr.ret
            if (rc != N):
                print("Error Reading Samples from Device (error code = %d)!" % rc)
                break;

            I,Q, alpha_est, sin_phi_est, cos_phi_est, I_new, Q_new = estimate_correct(rx_buff)

            plt.figure(1).clf()
            fig = plt.gcf()
            DPI = fig.get_dpi()
            fig.set_size_inches(404*2/float(DPI), 404/float(DPI))
            plt.subplot(121)
            plt.scatter(I, Q)
            plt.title('I/Q original')
            plt.subplot(122)
            plt.scatter(I_new, Q_new)
            plt.title('I/Q corrected')
            plt.grid()
            draw_figure_with_toolbar(window['fig_cv'].TKCanvas, fig, window['controls_cv'].TKCanvas)
            

    # Stop streaming and close the connection to the radio
    window.close()
    sdr.deactivateStream(rx_stream)
    sdr.deactivateStream(cw_tx_stream)
    sdr.closeStream(rx_stream)



###########################################################
## Global Area

# Enumerate the Soapy devices
#res = SoapySDR.Device.enumerate()
#for result in res:
#    print(result)
main()
