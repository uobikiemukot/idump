CC      = gcc
LDFLAGS =
CFLAGS  = -Wall -Wextra -std=c99 -pedantic \
	-march=native -Os -pipe -s

HDR = ../stb_image.h ../libnsgif.h ../libnsbmp.h ../lodepng.h
SRC = ../libnsgif.c ../libnsbmp.c ../lodepng.c
DST = idump sdump yaimgfb

SIXEL_SRC = libsixel/dither.c libsixel/fromsixel.c libsixel/image.c \
	libsixel/output.c libsixel/quant.c libsixel/tosixel.c
SIXEL_OBJ = dither.o fromsixel.o image.o \
	output.o quant.o tosixel.o

all: $(DST)

idump: idump.c $(HDR) $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $< -o $@

sdump: sdump.c libsixel.a $(HDR) $(SRC)
	$(CC) -static $(CFLAGS) $(LDFLAGS) \
		-I./libsixel -L./ \
		$(SRC) $< -o $@ libsixel.a

libsixel.a:
	$(CC) -c -fPIC -O3 $(SIXEL_SRC)
	ar ruv $@ $(SIXEL_OBJ)

yaimgfb: yaimgfb.c $(HDR) $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) $(SRC) $< -o $@

clean:
	rm -f $(DST) libsixel.a *.o
