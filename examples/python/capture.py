#-----------------------
#		IMPORTS
#-----------------------
from PySimpleGUI.PySimpleGUI import Canvas, Column
from PySimpleGUI import Window, WIN_CLOSED, Slider, Button, theme, Text, Radio, Image, InputText, Canvas
import matplotlib.pyplot as plt
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg, NavigationToolbar2Tk
import numpy as np
from numpy.lib.arraypad import pad
import time
import SoapySDR
from SoapySDR import SOAPY_SDR_RX, SOAPY_SDR_TX, SOAPY_SDR_CS16

#-------------------------
#			GUI
#-------------------------
def create_window(sensors):
    sensors_ctrls = []
    for i in range(len(sensors)):
        sensors_ctrls.append(Text(sensors[i] + ": "))
        sensors_ctrls.append(InputText('0.0', key=sensors[i]))
        
    layout = [
        [Column(
            layout=[
                [Text('Receiver Freq. [Hz]:'), InputText('868000000', key='RxFreq'), Text('RX Bandwidth: 2.5 MHz')],
            ]
        )],
        [Canvas(key='controls_cv')],
        [Column(
            layout=[
                [Canvas(key='fig_cv', size=(1000, 600))]
            ],
            background_color='#DAE0E6',
            pad=(0,0)
        )],
        sensors_ctrls,
        [Button("Exit", size=(10,1)), Button("Run", size=(10,1))],
    ]
    window = Window("CaribouLite Capture", layout, location=(1000,700))
    return window


def update_iq_graphs(window, I, Q, f, psd):
    plt.figure(1).clf()
    fig = plt.gcf()
    DPI = fig.get_dpi()
    
    fig.set_size_inches(304*4/float(DPI), 304/float(DPI))
    axs = fig.add_subplot(121)
    plt.plot(I, label="I")
    plt.plot(Q, label="Q")
    plt.legend(loc='upper center')
    plt.title('I/Q')

    plt.subplot(122)
    plt.plot(f, psd)
    plt.title('PSD')

    plt.grid()
    draw_figure_with_toolbar(window['fig_cv'].TKCanvas, fig, window['controls_cv'].TKCanvas)
    
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

#-------------------------
#		SOAPY
#-------------------------
def setup_receiver(sdr, freq_hz):
	use_agc = False
	sdr.setGainMode(SOAPY_SDR_RX, 0, use_agc)
	sdr.setGain(SOAPY_SDR_RX, 0, 0)
	sdr.setBandwidth(SOAPY_SDR_RX, 0, 2500e5)
	sdr.setFrequency(SOAPY_SDR_RX, 0, freq_hz)
	rx_stream = sdr.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16, [0])
	return rx_stream


def update_receiver_freq(sdr, stream, freq_hz):
    sdr.setFrequency(SOAPY_SDR_RX, 0, freq_hz)

class Toolbar(NavigationToolbar2Tk):
    def __init__(self, *args, **kwargs):
        super(Toolbar, self).__init__(*args,**kwargs)

def calculate_psd(I, Q, fs):
    x = I + 1j*Q
    N = (int)(len(I) / 16)
    x = x[0:N]
    x = x * np.hamming(len(x)) # apply a Hamming window
    PSD = (np.abs(np.fft.fft(x))/N)**2
    PSD_log = 10.0*np.log10(PSD)
    PSD_shifted = np.fft.fftshift(PSD_log)
    f = np.arange(fs/-2.0, fs/2.0, fs/N) # start, stop, step.  centered around 0 Hz
    return f,PSD_shifted



#-----------------------
#		MAIN
#-----------------------
def main():
    # Buffer Parameters
    N = 131072
    Fs = 4e6
    rx_buff = np.empty(2 * N, np.int16)  # Create memory buffer for data stream

    #  Initialize CaribouLite Soapy
    #sdr = SoapySDR.Device({"driver": "Cariboulite", "channel": "S1G"})
    sdr = SoapySDR.Device({"driver": "Cariboulite", "channel": "HiF"})
    rx_stream = setup_receiver(sdr, 900e6)
    sensors_list = sdr.listSensors(SOAPY_SDR_RX, 0)
    
    # Create the window
    window = create_window(sensors_list)
    while True:
        event, values = window.read(timeout = 20)

        #---------------------------------------------
        if (event == "Exit" or event == WIN_CLOSED):
            break;
            
        #---------------------------------------------
        elif event == "Run":
            print("clicked run")
            rx_freq = float(values['RxFreq'])
            update_receiver_freq(sdr, rx_stream, rx_freq)
            
            sdr.activateStream(rx_stream)
            
            for ii in range(10):
                sr = sdr.readStream(rx_stream, [rx_buff], N, timeoutUs=int(5e6))
                
            sr = sdr.readStream(rx_stream, [rx_buff], N, timeoutUs=int(5e6))
            if (sr.ret != N):
                    print("Error Reading Samples from Device (error code = %d)!" % sr.ret)
                    continue;
                    
            sdr.deactivateStream(rx_stream)

            s_real = rx_buff[::2].astype(np.float32)
            s_imag = rx_buff[1::2].astype(np.float32)
            
            f, psd = calculate_psd(s_real, s_imag, Fs)
            
            update_iq_graphs(window, s_real, s_imag, f, psd)
            
        else:
            for i in range(len(sensors_list)):
                values[sensors_list[i]] = str(sdr.readSensor(SOAPY_SDR_RX, 0, sensors_list[i]))
            

    # Stop streaming and close the connection to the radio
    window.close()
    sdr.closeStream(rx_stream)



# run the program
if __name__ == '__main__':
	main()