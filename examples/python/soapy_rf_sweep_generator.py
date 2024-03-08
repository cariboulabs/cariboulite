#-------------------------------
#   CaribouLite Simple RF Sweep Generator for CaribouLite HiF channel
#	Sweeps a range of RF frequyencies on HiF channel (1-6GHz capable)
#	  User interface sets sweep start freq (in KHz), end freq (in KHz), sweep rate (in ms), and step size (in Hz)
#	  by K7MDL March 7, 2024
#	  derived from soapy_synth.py example program
#	  requires PySimpleGUI - use pip to download anmd install the package
#-------------------------------

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
			[Text('Sweep Start Frequency [KHz]'), 
				InputText(freq_start, key='Start_TxFreq', size=(10,1)), 
				Button("Set Start", size=(10,1))],
		    ]	
		)],
		[Column(
		    layout=[
			[Text('Sweep End Frequency [KHz] '), 
				InputText(freq_end, key='End_TxFreq', size=(10,1)),
				Button("Set End", size=(10,1))],
		    ]
		)],
		[Column(
		    layout=[
			[Text('Step Size [Hz] '), 
				InputText(step_Hz, key='Set_StepHz', size=(10,1)),
				Button("Set Step Size", size=(10,1))],
		    ]
		)],
		[Column(
		    layout=[
			[Text('Step Rate [ms]'), 
				InputText(step_Rate, key='Set_StepRate', size=(10,1)),
				Button("Set Step Rate", size=(10,1))],
		    ]
		)],
		[Column(
		    layout=[				 
			[Text('Power [dBm]:'), 
				InputText(pwr, key='HiFTxPwr', size=(5,1)), 
				Button("Set Power", size=(10,1))],
		    ]
		)],
		[Button("Activate HiF", size=(10,1)), Checkbox("Active", key="ActiveHiF", default=False)],
		[Button("Exit", size=(10,1)),] 
	]
	window = Window("CaribouLite RF Sweep Generator", layout, margins=(10,10), location=(800,800))
	return window

#-----------------------
#		SOAPY
#-----------------------
def setup_freq_power(sdr, stream, freq_KHz, power=10.0):
	sdr.deactivateStream(stream)
	sdr.setFrequency(SOAPY_SDR_TX, 0, freq_KHz)
	sdr.setGain(SOAPY_SDR_TX, 0, power+10)

def setup_transmitter(sdr, freq_KHz=2304100):
	stream = sdr.setupStream(SOAPY_SDR_TX, SOAPY_SDR_CS16, [0], dict(CW="1"))
	setup_freq_power(sdr, stream, freq_KHz)
	return stream

def update_transmitter_freq(sdr, stream, channel, freq_hz):
	sdr.deactivateStream(stream)
	sdr.setFrequency(SOAPY_SDR_TX, channel, freq_hz)
	sdr.activateStream(stream)
    
#-----------------------
#		MAIN
#-----------------------
def main():
	activeHiF = False
	
	# Frequency is in KHz for data entry, SDR needs Hz values.
	global freq_start
	global freq_end
	freq_KHz = 2304100000.0
	# power is in dBm
	global pwr
	#Step Size in Hz
	global step_Hz
	#Step Rate in ms
	global step_Rate
		
	#  Initialize CaribouLite Soapy
	sdrHiF = SoapySDR.Device({"driver": "Cariboulite", "channel": "HiF"})
	synthStreamHiF = setup_transmitter(sdrHiF, freq_KHz)

	# create the window
	window = create_window()
	
	while True:
		event, values = window.read(timeout = step_Rate)   #20)

		#---------------------------------------------
		if (event == "Exit" or event == WIN_CLOSED):
			break;

		#---------------------------------------------
		elif event == "Set Power":
			pwr = float(values['HiFTxPwr'])
			setup_freq_power(sdrHiF, synthStreamHiF, freq_KHz, pwr)
			print("Current Freq: %.2f Hz, Pwr: %.1f dBm" % (freq_KHz, pwr))

		#---------------------------------------------
		elif event == "Set Step Size":
			step_Hz = float(values['Set_StepHz'])
			print("New Step Size: %.2f Hz" % (step_Hz))

		#---------------------------------------------
		elif event == "Set Step Rate":
			step_Rate = float(values['Set_StepRate'])
			print("New Step Rate: %dms" % (step_Rate))

		#---------------------------------------------
		elif event == "Set Start":
			freq_start = float(values['Start_TxFreq'])
			freq_KHz = freq_start * 1000
			print("Set Sweep Start Freq: %.2f Hz, Pwr: %.1f dBm" % (freq_start, pwr))

		#---------------------------------------------
		elif event == "Set End":
			freq_end = float(values['End_TxFreq'])
			print("Set Sweep End Freq: %.2f Hz, Pwr: %.1f dBm" % (freq_end, pwr))
			
		elif event == "Activate HiF":
			activeHiF = not activeHiF
			if activeHiF:
				if (freq_KHz > freq_end*1000):
					freq_KHz = freq_start*1000
				sdrHiF.activateStream(synthStreamHiF)
				setup_freq_power(sdrHiF, synthStreamHiF, freq_KHz, pwr)
		
			else:
				setup_freq_power(sdrHiF, synthStreamHiF, freq_KHz, pwr)
				sdrHiF.deactivateStream(synthStreamHiF)

		window["ActiveHiF"].Update(value=activeHiF)

		if (activeHiF):
			freq_KHz = freq_KHz + step_Hz
			if (freq_KHz > freq_end*1000):
				freq_KHz = freq_start*1000
			update_transmitter_freq(sdrHiF, synthStreamHiF, 0, freq_KHz)
			#print("Sweep Freq: %.2f Hz, Start: %d KHz, End: %d KHz, Step Size: %d, Step Rate: %d, Pwr: %.1f dBm" % (freq_KHz, freq_start, freq_end, step_Hz, step_Rate, pwr))

	sdrHiF.deactivateStream(synthStreamHiF)
	sdrHiF.closeStream(synthStreamHiF)
	window.close()

#Globals
# frequency is in KHz for data entry, SDR needs Hz values.
freq_start = 2304090
freq_end = 2304110
# power is in dBm
pwr = 14.0
#Step Size in Hz
step_Hz = 500
#Step Rate in ms
step_Rate = 10

# run the program
if __name__ == '__main__':
	main()
