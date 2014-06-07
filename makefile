SHELL = /bin/bash
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic \
-march=native -Os -pipe -s
#-O0 -g -pg
#LDFLAGS = -lpng -ljpeg -ltiff -lstimg

HDR = util.h stb_image.h #lodepng.h
SRC = #lodepng.c
DST = idump

all:  $(DST)

idump: idump.c $(HDR) $(SRC)
	$(CC) $(CFLAGS) $< $(SRC) -o $@

.c.o:
	$(CC) $(CFLAGS) -DHAVE_CONFIG_H -c $<

clean:
	rm -f $(DST) *.o
