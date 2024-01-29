#!/usr/bin/python3

# Calculates the coefficients for a Bessel filter

from scipy import signal
# sampling rate
fs = 1000
# cutoff
f0 = 100
# order
order = 8
sos = signal.bessel(order, f0/fs*2, 'low', output='sos')
for s in sos:
    print("{",end="")
    n = 0
    for c in s:
        print("%.18e" % c,end="")
        n=n+1
        if n<6:
            print(",",end="")
    print("},")
