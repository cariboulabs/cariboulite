# GNU Make >= 4.3 (grouped outputs), matching the validated iCE40 build flow.
.DEFAULT_GOAL := build
SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c

YOSYS ?= yosys
NEXTPNR ?= nextpnr-ice40
ICEPACK ?= icepack
BLOBGEN ?= ../software/utils/generate_bin_blob
PROG ?= ../software/libcariboulite/build/test/ice40programmer

HDL := $(wildcard *.v)
PCF := io.pcf
HEADER := h-files/cariboulite_fpga_firmware.h
LIB_HEADER ?= ../software/libcariboulite/src/cariboulite_fpga_firmware.h

.PHONY: build prog prog_only clean
build: top.blif top.bin $(HEADER) $(LIB_HEADER)

# Publish outputs only after the tool succeeds. Failed attempts retain the
# last successful artifacts and leave their prerequisites newer for retry.
top.json top.blif &: $(HDL) Makefile
	@tmp=$$(mktemp -d .synth.XXXXXX); trap 'rm -rf "$$tmp"' EXIT; \
	$(YOSYS) -Q -T -p "synth_ice40 -top top -json $$tmp/top.json -blif $$tmp/top.blif" top.v > synthesis.log 2>&1 || { cat synthesis.log; exit 1; }; \
	test -s "$$tmp/top.json"; test -s "$$tmp/top.blif"; \
	mv "$$tmp/top.json" top.json; mv "$$tmp/top.blif" top.blif

top.asc nextpnr_timing.json &: top.json $(PCF) Makefile
	@tmp=$$(mktemp -d .pnr.XXXXXX); trap 'rm -rf "$$tmp"' EXIT; \
	$(NEXTPNR) --lp1k --package qn84 --json top.json --pcf $(PCF) \
	  --asc "$$tmp/top.asc" --parallel-refine --opt-timing --seed 16 \
	  --no-promote-globals --report "$$tmp/timing.json" > nextpnr.log 2>&1 || { cat nextpnr.log; exit 1; }; \
	test -s "$$tmp/top.asc"; test -s "$$tmp/timing.json"; \
	mv "$$tmp/top.asc" top.asc; mv "$$tmp/timing.json" nextpnr_timing.json

top.bin: top.asc nextpnr_timing.json Makefile
	@tmp=$$(mktemp -d .pack.XXXXXX); trap 'rm -rf "$$tmp"' EXIT; \
	$(ICEPACK) top.asc "$$tmp/top.bin"; test -s "$$tmp/top.bin"; \
	mv "$$tmp/top.bin" top.bin

$(HEADER): top.bin $(BLOBGEN) Makefile
	@mkdir -p h-files; tmp=$$(mktemp -d h-files/.blob.XXXXXX); trap 'rm -rf "$$tmp"' EXIT; \
	$(BLOBGEN) top.bin cariboulite_firmware "$$tmp/cariboulite_fpga_firmware.h"; \
	test -s "$$tmp/cariboulite_fpga_firmware.h"; \
	mv "$$tmp/cariboulite_fpga_firmware.h" "$@"

$(LIB_HEADER): $(HEADER)
	@mkdir -p "$(@D)"; tmp=$$(mktemp "$(@D)/.firmware.XXXXXX"); trap 'rm -f "$$tmp"' EXIT; \
	cp "$<" "$$tmp"; mv "$$tmp" "$@"

# Programming remains explicit; a normal build never accesses the board.
prog: build
	$(PROG) top.bin

prog_only:
	$(PROG) top.bin

clean:
	rm -f top.json top.blif top.asc top.bin nextpnr_timing.json synthesis.log nextpnr.log $(HEADER)
