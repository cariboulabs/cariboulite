#! /usr/bin/bash

# Decimate
FSTART="125"
FSTOP="125"
MOD="top"

f=${FSTART}
while [ $f -le ${FSTOP} ]
  do
  i=1
  while [ $i -le 30 ]
    do
      echo "freq $f seed $i"
      nextpnr-ice40 --lp1k --package qn84 --asc ${MOD}.asc --pcf io.pcf --json ${MOD}.json --seed $i --parallel-refine --opt-timing --timing-allow-fail 2>&1 | fgrep 'Info: Max frequency for clock'
      
      icetime -d lp1k -P qn84 -p io.pcf -t ${MOD}.asc 2>&1 | fgrep 'Total path delay'
      #icepack ${MOD}.asc ${MOD}.bin
      i=$((i+1))
    done
  f=$((f+5))
  done
