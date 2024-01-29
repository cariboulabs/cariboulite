import scipy.signal as signal
import numpy as np

sos = signal.butter(2, 0.1, output='sos')
print(sos)

x = np.array([-1,0.5,1,0.5,0.3,-77,1E-5])

y = signal.sosfilt(sos, x)

print(y)

print("----------------------")

sos = signal.butter(4, 0.15, output='sos')
print(sos)

x = np.array([-1,0.5,-1,0.5,-0.3,3,-1E-5])
y = signal.sosfilt(sos, x)

print(y)
