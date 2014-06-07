CC      = gcc
LDFLAGS = -lm
CFLAGS  = -Wall -Wextra -std=c99 -pedantic \
	-march=native -Os -pipe -s

HDR = stb_image.h
DST = idump

all:  $(DST)

idump: idump.c $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

clean:
	rm -f $(DST) *.o
