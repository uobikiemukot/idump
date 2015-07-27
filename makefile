CC      ?= gcc
LDFLAGS ?= -lpng -ljpeg -L/usr/local/lib 
CFLAGS  ?= -Wall -Wextra -std=c99 -pedantic \
	-O3 -pipe -s \
	-I/usr/local/include

HDR = stb_image.h libnsgif.h libnsbmp.h
SRC = libnsgif.c libnsbmp.c
DST = idump

all:  $(DST)

idump: idump.c $(HDR) $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $< -o $@

clean:
	rm -f $(DST)
