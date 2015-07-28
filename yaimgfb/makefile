CC      ?= gcc
LDFLAGS ?= -lpng -ljpeg -ltiff -L/usr/local/lib
CFLAGS  ?= -Wall -Wextra -std=c99 -pedantic \
	-O3 -pipe -s \
	-I/usr/local/include

HDR = ../stb_image.h ../libnsgif.h ../libnsbmp.h
SRC = ../libnsgif.c ../libnsbmp.c
DST = yaimgfb

all:  $(DST)

yaimgfb: yaimgfb.c $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $< -o $@

clean:
	rm -f $(DST)
