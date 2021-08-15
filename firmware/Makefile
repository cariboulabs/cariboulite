PROG = ../software/libcariboulite/build/test/ice40programmer
filename = top
pcf_file = ./io.pcf

build:
	yosys -p 'synth_ice40 -top top -json $(filename).json' $(filename).v
	nextpnr-ice40 --lp1k --package qn84 --json $(filename).json --pcf $(pcf_file) --asc $(filename).asc
	icepack $(filename).asc $(filename).bin

#build:
#	yosys -p "synth_ice40 -blif $(filename).blif" $(filename).v
#	arachne-pnr -d 1k -P qn84 -p $(pcf_file) $(filename).blif -o $(filename).asc
#	icepack $(filename).asc $(filename).bin

prog: build
	 $(PROG) $(filename).bin

prog_only:
	$(PROG) $(filename).bin

clean:
	rm -rf $(filename).json $(filename).blif $(filename).asc $(filename).bin
