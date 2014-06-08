CC      = gcc
LDFLAGS = -lm -lpng -ljpeg
CFLAGS  = -Wall -Wextra -std=c99 -pedantic \
	-march=native -Os -pipe -s

HDR = stb_image.h libnsgif.h libnsbmp.h lodepng.h
SRC = libnsgif.c libnsbmp.c lodepng.c
DST = idump

all:  $(DST)

idump: idump.c $(HDR) $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $< -o $@

idump_no_extralib: idump_no_extra_lib.c $(HDR)
	$(CC) $(CFLAGS) -lm $(SRC) $< -o $@

clean:
	rm -f $(DST) *.o
