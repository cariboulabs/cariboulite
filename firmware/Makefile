PROG = ../software/libcariboulite/build/test/ice40programmer
filename = top
pcf_file = ./io.pcf

top.bin:
	yosys -p 'synth_ice40 -top top -json $(filename).json' $(filename).v
	nextpnr-ice40 --lp1k --package qn84 --json $(filename).json --pcf $(pcf_file) --asc $(filename).asc
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
