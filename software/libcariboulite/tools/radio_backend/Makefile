# Independent executable: leave the baseline CMake graph and test_app untouched.
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../../../..)
SRC := $(ROOT)/software/libcariboulite/src
HERE := $(ROOT)/software/libcariboulite/tools/radio_backend
OUT ?= $(ROOT)/build/radio-backend
LIBDIR ?= $(ROOT)/build
CC ?= cc
CPPFLAGS += -D_GNU_SOURCE -D_POSIX_C_SOURCE=200809L -I$(SRC) -I$(HERE)
CFLAGS := -std=c11 -O3 -g -Wall -Wextra -Wno-unused-parameter -Wno-missing-braces
DSPFLAGS := -ffast-math -fno-math-errno -funroll-loops
BASE_NAMES := rx_pipeline pipeline_transport pipeline_runtime nbfm_demod nbfm_demod_dsp wbfm_demod noise_squelch carrier_squelch alsa_sink
OWN_NAMES := main modem_snapshot status_publisher
OBJECTS := $(addprefix $(OUT)/,$(addsuffix .o,$(BASE_NAMES) $(OWN_NAMES))) $(OUT)/demod_worker.o

.PHONY: all clean test
all: $(OUT)/cariboulite_rx_backend

$(OUT):
	mkdir -p "$@"

$(OUT)/cariboulite_rx_backend: $(OBJECTS) $(LIBDIR)/libcariboulite.so
	$(CC) $(OBJECTS) -L$(LIBDIR) -Wl,-rpath,$(LIBDIR) -lcariboulite -lasound -lpthread -lm -o "$@.tmp"
	mv "$@.tmp" "$@"

# Change the diagnostic label only in a generated backend compilation copy.
# The baseline worker and test_app retain their original source and behavior.
$(OUT)/demod_worker_backend.c: $(SRC)/demod_worker.c $(HERE)/Makefile | $(OUT)
	sed 's/ aud_fifo=/ audio_fifo=/' "$<" > "$@"

$(OUT)/demod_worker.o: $(OUT)/demod_worker_backend.c | $(OUT)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DSPFLAGS) -MMD -MP -c "$<" -o "$@"

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
