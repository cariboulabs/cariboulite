PROG = ../software/libcariboulite/build/test/ice40programmer
filename = top
pcf_file = ./io.pcf

SOURCES := $(wildcard *.v)

top.bin: $(SOURCES)
	yosys -p 'synth_ice40 -abc2 -top top -json $(filename).json -blif $(filename).blif' -p 'opt' -p 'ice40_opt' -p 'fsm_opt' $(filename).v | grep -i warning
	#nextpnr-ice40 --lp1k --package qn84 --json $(filename).json --pcf $(pcf_file) --asc $(filename).asc
	#false; while [ $$? != 0 ]; do nextpnr-ice40 --lp1k --package qn84 --json $(filename).json --pcf $(pcf_file) --asc $(filename).asc --freq 64 --opt-timing; done
	nextpnr-ice40 --lp1k --package qn84 --json $(filename).json --pcf $(pcf_file) --asc $(filename).asc --freq 80 --parallel-refine --opt-timing --seed 5
	#nextpnr-ice40 --json blinky.json --pcf blinky.pcf --asc blinky.asc --gui
	icepack $(filename).asc $(filename).bin

build: top.bin
	echo "Generating code blob"
	../software/utils/generate_bin_blob ./top.bin cariboulite_firmware ./h-files/cariboulite_fpga_firmware.h

	echo "Copying firmware blob to the software lib"
	cp ./h-files/cariboulite_fpga_firmware.h ../software/libcariboulite/src/

prog: build
	 $(PROG) $(filename).bin

prog_only:
	$(PROG) $(filename).bin

clean:
	rm -rf $(filename).json $(filename).blif $(filename).asc $(filename).bin
