#-----------------------
#		IMPORTS
#-----------------------
# PySimpleGUI
from PySimpleGUI.PySimpleGUI import Canvas, Column
from PySimpleGUI import Window, WIN_CLOSED, Slider, \
					Button, theme, Text, Radio, Image, InputText, Canvas, Checkbox

# System
import time

# Soapy
import SoapySDR
from SoapySDR import SOAPY_SDR_RX, SOAPY_SDR_TX, SOAPY_SDR_CS16

#-------------------------
#			GUI
#-------------------------
def create_window():
    layout = [
		[Column(
            layout=[
                [Text('Lo Channel Frequency [Hz]'), 
				 InputText('900000000', key='LoFTxFreq'), 
				 Text('Power [dBm]:'), 
				 InputText('23', key='LoFTxPwr'), 
				 Button("Set S1G", size=(10,1)), Button("Activate S1G", size=(10,1)), Checkbox("Active", key="ActiveS1G", default=False)],
            ]
        )],
        [Column(
            layout=[
                [Text('Hi Channel Frequency [Hz]'), 
				 InputText('900000000', key='HiFTxFreq'), 
				 Text('Power [dBm]:'), 
				 InputText('23', key='HiFTxPwr'), 
				 Button("Set HiF", size=(10,1)), Button("Activate HiF", size=(10,1)), Checkbox("Active", key="ActiveHiF", default=False)],
            ]
        )],
        [Button("Exit", size=(10,1))],
    ]
    window = Window("CaribouLite Synthesizer", layout, location=(800,400))
    return window

#-----------------------
#		SOAPY
#-----------------------
def setup_freq_power(sdr, stream, freq, power=14.0):
	sdr.deactivateStream(stream)
	sdr.setFrequency(SOAPY_SDR_TX, 0, freq)
	sdr.setGain(SOAPY_SDR_TX, 0, power+10)

def setup_transmitter(sdr, freq_hz=900e6):
	stream = sdr.setupStream(SOAPY_SDR_TX, SOAPY_SDR_CS16, [0], dict(CW="1"))
	setup_freq_power(sdr, stream, freq_hz)
	return stream

#-----------------------
#		MAIN
#-----------------------
def main():
	#  Initialize CaribouLite Soapy
	sdrS1G = SoapySDR.Device({"driver": "Cariboulite", "channel": "S1G"})
	sdrHiF = SoapySDR.Device({"driver": "Cariboulite", "channel": "HiF"})
	
	synthStreamS1G = setup_transmitter(sdrS1G, 900e6)
	synthStreamHiF = setup_transmitter(sdrHiF, 2400e6)

	activeHiF = False
	activeS1G = False

	# create the window
	window = create_window()
	while True:
		event, values = window.read(timeout = 20)

		#---------------------------------------------
		if (event == "Exit" or event == WIN_CLOSED):
			break;

		#---------------------------------------------
		elif event == "Set S1G":
			freq = float(values['LoFTxFreq'])
			pwr = float(values['LoFTxPwr'])
			print("Set S1G Freq: %.2f Hz, Pwr: %.1f dBm" % (freq, pwr))
			setup_freq_power(sdrS1G, synthStreamS1G, freq, pwr)
			activeS1G = False

		#---------------------------------------------
		elif event == "Set HiF":
			freq = float(values['HiFTxFreq'])
			pwr = float(values['HiFTxPwr'])
			print("Set HiF Freq: %.2f Hz, Pwr: %.1f dBm" % (freq, pwr))
			setup_freq_power(sdrHiF, synthStreamHiF, freq, pwr)
			activeHiF = False

		#---------------------------------------------
		elif event == "Activate S1G":
			activeS1G = not activeS1G
			if activeS1G:
				sdrS1G.activateStream(synthStreamS1G)
			else:
				sdrS1G.deactivateStream(synthStreamS1G)

		#---------------------------------------------
		elif event == "Activate HiF":
			activeHiF = not activeHiF
			if activeHiF:
				sdrHiF.activateStream(synthStreamHiF)
			else:
				sdrHiF.deactivateStream(synthStreamHiF)

		window["ActiveHiF"].Update(value=activeHiF)
		window["ActiveS1G"].Update(value=activeS1G)

	sdrS1G.deactivateStream(synthStreamS1G)
	sdrHiF.deactivateStream(synthStreamHiF)
	sdrS1G.closeStream(synthStreamS1G)
	sdrHiF.closeStream(synthStreamHiF)
	window.close()


# run the program
if __name__ == '__main__':
	main()