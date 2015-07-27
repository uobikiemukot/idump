CC      ?= gcc
LDFLAGS = -L /usr/local/lib -lpng -ljpeg
CFLAGS  = -Wall -Wextra -std=c99 -pedantic \
	-march=native -Os -pipe -s \
	-I /usr/local/include

HDR = stb_image.h libnsgif.h libnsbmp.h
SRC = libnsgif.c libnsbmp.c
DST = idump

all:  $(DST)

idump: idump.c $(HDR) $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $< -o $@

clean:
	rm -f $(DST)
