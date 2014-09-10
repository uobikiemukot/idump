CC      ?= gcc
LDFLAGS ?= -lpng -ljpeg
CFLAGS  ?= -Wall -Wextra -std=c99 -pedantic \
	-march=native -Os -pipe -s

HDR = stb_image.h libnsgif.h libnsbmp.h
SRC = libnsgif.c libnsbmp.c
DST = idump

all:  $(DST)

idump: idump.c $(HDR) $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $< -o $@

sdump: sdump.c libnsgif.c libnsbmp.c $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) -lsixel libnsgif.c libnsbmp.c $< -o $@

clean:
	rm -f $(DST)
