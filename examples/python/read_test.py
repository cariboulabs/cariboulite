from PySimpleGUI.PySimpleGUI import Canvas, Column
from PySimpleGUI import Window, WIN_CLOSED, Slider, Button, theme, Text, Radio, Image, InputText, Canvas
import numpy as np
import matplotlib.pyplot as plt
import time
import SoapySDR
from SoapySDR import SOAPY_SDR_RX, SOAPY_SDR_TX, SOAPY_SDR_CS16

"""
	Build a dictionaly of parameters
"""
def MakeParameters():
	params = {	"DriverName": "HermonSDR",
				"RxChannel": 0,
				"NumOfComplexSample": 4*16384,
				"RxFrequencyHz": 915e6,
				"UseAGC": True,
				"Gain": 50.0,
				"RXBandwidth": 0.02e6,
				#"Frontend": "TX/RX ANT2 LNA-Bypass"}
				"Frontend": "TX/RX ANT2"}

	return params


"""
	Setup the RX channel
"""
def SetupReceiver(sdr, params):
	# Gain mode
	sdr.setGainMode(SOAPY_SDR_RX, params["RxChannel"], params["UseAGC"])

	# Set RX gain (if not using AGC)
	sdr.setGain(SOAPY_SDR_RX, params["RxChannel"], params["Gain"])

	# Rx Frequency
	sdr.setFrequency(SOAPY_SDR_RX, params["RxChannel"], params["RxFrequencyHz"])

	# Rx BW
	sdr.setBandwidth(SOAPY_SDR_RX, params["RxChannel"], params["RXBandwidth"])

	# Frontend select
	sdr.setAntenna(SOAPY_SDR_RX, params["RxChannel"], params["Frontend"])

	# Make the stream
	return sdr.setupStream(SOAPY_SDR_RX, SOAPY_SDR_CS16, [params["RxChannel"]])  # Setup data stream


"""
	Main
"""
if __name__ == "__main__":
	print("HermonSDR Sampling Test")

	params = MakeParameters()

	# Memory buffers
	samples = np.empty(2 * params["NumOfComplexSample"], np.int16)

	#  Initialize Soapy
	sdr = SoapySDR.Device(dict(driver=params["DriverName"]))
	rxStream = SetupReceiver(sdr, params=params)
	sdr.activateStream(rxStream)

	# Read samples into buffer
	for ii in range(1,10):
		sr = sdr.readStream(rxStream, [samples], params["NumOfComplexSample"], timeoutUs=int(5e6))
		rc = sr.ret
		if (rc != params["NumOfComplexSample"]):
			print("Error Reading Samples from Device (error code = %d)!" % rc)
			exit

	# convert to float complex
	I = samples[::2].astype(np.float32)
	Q = samples[1::2].astype(np.float32)
	complexFloatSamples = I + 1j*Q

	#for k in range(len(I)):
	#	if I[k] > 30 or I[k] < -30:
	#		print(I[k])

	# plot samples
	fig = plt.figure()
	plt.plot(I)
	plt.plot(Q)
	plt.show()

	print("Goodbye")
