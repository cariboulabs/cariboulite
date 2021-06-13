#
# Set CROSS_PREFIX to prepend to all compiler tools at once for easier
# cross-compilation.
CROSS_PREFIX =
CC           = $(CROSS_PREFIX)gcc
AR           = $(CROSS_PREFIX)ar
RANLIB       = $(CROSS_PREFIX)ranlib
SIZE         = $(CROSS_PREFIX)size
STRIP        = $(CROSS_PREFIX)strip
SHLIB        = $(CC) -shared
STRIPLIB     = $(STRIP) --strip-unneeded

CFLAGS	+= -O3 -Wall -pthread

LIB     = libpigpio.a

lib:	pigpio.c pigpio.h
	$(CC) $(CFLAGS) -fpic -c pigpio.c command.c
	$(AR) rcs libpigpio.a pigpio.o command.c

