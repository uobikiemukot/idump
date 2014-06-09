CC      = gcc
LDFLAGS = -lm -ljpeg -lpng
CFLAGS  = -Wall -Wextra -std=c99 -pedantic \
-march=native -Os -pipe -s
#-O0 -pg -g

HDR = ../lib/stb_image.h ../lib/libnsgif.h ../lib/libnsbmp.h
SRC = ../lib/libnsgif.c ../lib/libnsbmp.c
DST = yaimgfb

all:  $(DST)

yaimgfb: yaimgfb.c $(HDR)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $< -o $@

clean:
	rm -f $(DST) *.o
