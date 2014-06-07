SHELL = /bin/bash
CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pedantic \
-march=native -Os -pipe -s

HDR = stb_image.h
DST = idump

all:  $(DST)

idump: idump.c $(HDR)
	$(CC) $(CFLAGS) $< -o $@

.c.o:
	$(CC) $(CFLAGS) -DHAVE_CONFIG_H -c $<

clean:
	rm -f $(DST) *.o
