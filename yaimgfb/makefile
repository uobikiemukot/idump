CC      ?= gcc
LDFLAGS ?= -ljpeg -lpng
CFLAGS  ?= -Wall -Wextra -std=c99 -pedantic \
	-march=native -Os -pipe -s
	#-O0 -pg -g

HDR = ../stb_image.h ../libnsgif.h ../libnsbmp.h
SRC = ../libnsgif.c ../libnsbmp.c
DST = yaimgfb

all:  $(DST)

yaimgfb: yaimgfb.c $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $< -o $@

clean:
	rm -f $(DST)
