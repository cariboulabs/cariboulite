# Independent executable using the same instrumented RX workers as test_app.
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../..)
SRC := $(ROOT)/software/libcariboulite/src
HERE := $(ROOT)/software/libcariboulite/tools/radio_backend
OUT ?= $(ROOT)/build/radio-backend
LIBDIR ?= $(ROOT)/build
CC ?= cc
CPPFLAGS += -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -I$(SRC) -I$(HERE)
CFLAGS := -std=c11 -O3 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-braces
DSPFLAGS := -ffast-math -fno-math-errno -funroll-loops
BASE_NAMES := rx_pipeline pipeline_transport pipeline_runtime nbfm_demod nbfm_demod_dsp demod_worker wbfm_demod noise_squelch carrier_squelch alsa_sink
OWN_NAMES := main modem_snapshot status_publisher
OBJECTS := $(addprefix $(OUT)/,$(addsuffix .o,$(BASE_NAMES) $(OWN_NAMES)))

.PHONY: all clean test
all: $(OUT)/cariboulite_rx_backend

$(OUT):
	mkdir -p "$@"

$(OUT)/cariboulite_rx_backend: $(OBJECTS) $(LIBDIR)/libcariboulite.so
	$(CC) $(OBJECTS) -L$(LIBDIR) -Wl,-rpath,$(LIBDIR) -lcariboulite -lasound -lpthread -lm -o "$@.tmp"
	mv "$@.tmp" "$@"

$(addprefix $(OUT)/,$(addsuffix .o,$(OWN_NAMES))): $(OUT)/%.o: $(HERE)/%.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c "$<" -o "$@"

$(addprefix $(OUT)/,$(addsuffix .o,$(BASE_NAMES))): $(OUT)/%.o: $(SRC)/%.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DSPFLAGS) -MMD -MP -c "$<" -o "$@"

clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d) "$(OUT)/cariboulite_rx_backend" "$(OUT)/cariboulite_rx_backend.tmp" "$(OUT)/demod_worker_backend.c" "$(OUT)/test_status_publisher" "$(OUT)/test_modem_snapshot"

$(OUT)/test_status_publisher: $(HERE)/status_publisher.c $(HERE)/status_publisher.h $(HERE)/test_status_publisher.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror $(HERE)/status_publisher.c $(HERE)/test_status_publisher.c -o "$@"

$(OUT)/test_modem_snapshot: $(HERE)/modem_snapshot.c $(HERE)/modem_snapshot.h $(HERE)/test_modem_snapshot.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Werror $(HERE)/modem_snapshot.c $(HERE)/test_modem_snapshot.c -lpthread -o "$@"

test: all $(OUT)/test_status_publisher $(OUT)/test_modem_snapshot
	"$(OUT)/test_status_publisher"
	"$(OUT)/test_modem_snapshot"
	python3 "$(ROOT)/software/libcariboulite/tests/test_radio_backend.py"

-include $(OBJECTS:.o=.d)
