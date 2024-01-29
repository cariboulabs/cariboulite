#!/usr/bin/python3

# Calculates the coefficients for an elliptic filter

from scipy import signal
# sampling rate
fs = 1000
# cutoff
f0 = 100
# order
order = 4
# passband ripple
pr = 5
# minimum stopband rejection
sr = 40
sos = signal.ellip(order, pr, sr, f0/fs*2, 'low', output='sos')
for s in sos:
    print("{",end="")
    n = 0
    for c in s:
        print("%.18e" % c,end="")
        n=n+1
        if n<6:
            print(",",end="")
    print("},")
