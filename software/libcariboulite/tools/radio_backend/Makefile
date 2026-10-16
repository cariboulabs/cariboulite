# Independent radio applications sharing the instrumented workers with test_app.
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../..)
SRC := $(ROOT)/software/libcariboulite/src
HERE := $(ROOT)/software/libcariboulite/tools/radio_backend
OUT ?= $(ROOT)/build/radio-backend
LIBDIR ?= $(ROOT)/build
CC ?= cc
CPPFLAGS += -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -I$(SRC) -I$(HERE)
CFLAGS := -std=c11 -O3 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-braces
DSPFLAGS := -ffast-math -fno-math-errno -funroll-loops
BASE_NAMES := rx_pipeline pipeline_transport pipeline_runtime nbfm_demod nbfm_demod_dsp demod_worker wbfm_demod noise_squelch carrier_squelch alsa_sink tx_pipeline tx_audio_buffer mod_worker nbfm_mod alsa_source tone_source
OWN_NAMES := main modem_snapshot status_publisher command_protocol control_server
OBJECTS := $(addprefix $(OUT)/,$(addsuffix .o,$(BASE_NAMES) $(OWN_NAMES)))

.PHONY: all clean test
all: $(OUT)/cariboulite_radio_backend $(OUT)/cariboulite_radio_controller $(OUT)/cariboulite_radio_monitor

$(OUT):
	mkdir -p "$@"

$(OUT)/cariboulite_radio_backend: $(OBJECTS) $(LIBDIR)/libcariboulite.so
	$(CC) $(OBJECTS) -L$(LIBDIR) -Wl,-rpath,$(LIBDIR) -lcariboulite -lasound -lpthread -lm -o "$@.tmp"
	mv "$@.tmp" "$@"

$(OUT)/cariboulite_radio_controller: $(HERE)/radio_controller.c $(HERE)/command_protocol.c $(HERE)/command_protocol.h | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HERE)/radio_controller.c $(HERE)/command_protocol.c -lm -o "$@.tmp"
	mv "$@.tmp" "$@"

# Python resolves the source symlink before importing the adjacent register decoder.
$(OUT)/cariboulite_radio_monitor: $(HERE)/../cariboulite_radio_monitor.py | $(OUT)
	ln -sf "$<" "$@.tmp"
	mv "$@.tmp" "$@"

$(addprefix $(OUT)/,$(addsuffix .o,$(OWN_NAMES))): $(OUT)/%.o: $(HERE)/%.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(addprefix $(OUT)/,$(addsuffix .o,$(BASE_NAMES))): $(OUT)/%.o: $(SRC)/%.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DSPFLAGS) -MMD -MP -c "$<" -o "$@"

clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d) "$(OUT)/cariboulite_radio_backend" "$(OUT)/cariboulite_radio_backend.tmp" "$(OUT)/demod_worker_backend.c" "$(OUT)/test_status_publisher" "$(OUT)/test_modem_snapshot"
	rm -f "$(OUT)/cariboulite_radio_controller" "$(OUT)/cariboulite_radio_controller.tmp" "$(OUT)/cariboulite_radio_monitor" "$(OUT)/cariboulite_radio_monitor.tmp" "$(OUT)/test_command_protocol" "$(OUT)/test_control_server"
	rm -f "$(OUT)/cariboulite_rx_backend" "$(OUT)/cariboulite_rx_backend.tmp" "$(OUT)/cariboulite_radio_control" "$(OUT)/cariboulite_radio_control.tmp"

$(OUT)/test_status_publisher: $(HERE)/status_publisher.c $(HERE)/status_publisher.h $(HERE)/test_status_publisher.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror $(HERE)/status_publisher.c $(HERE)/test_status_publisher.c -o "$@"

$(OUT)/test_modem_snapshot: $(HERE)/modem_snapshot.c $(HERE)/modem_snapshot.h $(HERE)/test_modem_snapshot.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror $(HERE)/modem_snapshot.c $(HERE)/test_modem_snapshot.c -lpthread -o "$@"

$(OUT)/test_command_protocol: $(HERE)/command_protocol.c $(HERE)/command_protocol.h $(HERE)/test_command_protocol.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror $(HERE)/command_protocol.c $(HERE)/test_command_protocol.c -lm -o "$@"

$(OUT)/test_control_server: $(HERE)/control_server.c $(HERE)/control_server.h $(HERE)/command_protocol.c $(HERE)/status_publisher.c $(HERE)/test_control_server.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror $(HERE)/control_server.c $(HERE)/command_protocol.c $(HERE)/status_publisher.c $(HERE)/test_control_server.c -lm -o "$@"

test: all $(OUT)/test_status_publisher $(OUT)/test_modem_snapshot $(OUT)/test_command_protocol $(OUT)/test_control_server
	"$(OUT)/test_status_publisher"
	"$(OUT)/test_modem_snapshot"
	"$(OUT)/test_command_protocol"
	"$(OUT)/test_control_server"
	python3 "$(ROOT)/software/libcariboulite/tests/test_radio_backend.py"
	python3 "$(ROOT)/software/libcariboulite/tests/test_rx_control_backend.py"
	python3 "$(ROOT)/software/libcariboulite/tests/test_radio_control.py"
	python3 "$(ROOT)/software/libcariboulite/tests/test_tx_control_backend.py"
	python3 "$(ROOT)/software/libcariboulite/tests/test_fpga_control_backend.py"

-include $(OBJECTS:.o=.d)
