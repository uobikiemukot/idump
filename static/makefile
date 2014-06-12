CC      = gcc
LDFLAGS =
CFLAGS  = -Wall -Wextra -std=c99 -pedantic \
	-march=native -Os -pipe -s

HDR = ../lib/stb_image.h ../lib/libnsgif.h ../lib/libnsbmp.h ../lib/lodepng.h
SRC = ../lib/libnsgif.c ../lib/libnsbmp.c ../lib/lodepng.c
DST = idump yaimgfb

all:  $(DST)

idump: idump.c $(HDR) $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $< -o $@

yaimgfb: yaimgfb.c $(HDR) $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $< -o $@

clean:
	rm -f $(DST) *.o
