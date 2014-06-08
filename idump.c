/*
	The MIT License (MIT)

	Copyright (c) 2014 haru <uobikiemukot at gmail dot com>

	Permission is hereby granted, free of charge, to any person obtaining a copy
	of this software and associated documentation files (the "Software"), to deal
	in the Software without restriction, including without limitation the rights
	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
	copies of the Software, and to permit persons to whom the Software is
	furnished to do so, subject to the following conditions:

	The above copyright notice and this permission notice shall be included in
	all copies or substantial portions of the Software.

	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
	THE SOFTWARE.
*/
#define _XOPEN_SOURCE 600
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fb.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* for gif/bmp/(ico not supported) */
#include "libnsgif.h"
#include "libnsbmp.h"

/* for jpeg */
#include <jpeglib.h>

/* for png */
#include <png.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

const char *fb_path = "/dev/fb0";
char temp_file[] = "/tmp/idump.XXXXXX";

enum {
	DEBUG            = true,
	BITS_PER_BYTE    = 8,
	BUFSIZE          = 1024,
	MULTIPLER        = 1024,
	BYTES_PER_PIXEL  = 4,
	PNG_HEADER_SIZE  = 8,
	CMAP_COLORS      = 256,
	CMAP_RED_SHIFT   = 5,
	CMAP_GREEN_SHIFT = 2,
	CMAP_BLUE_SHIFT  = 0,
	CMAP_RED_MASK    = 3,
	CMAP_GREEN_MASK  = 3,
	CMAP_BLUE_MASK   = 2
};

const uint32_t bit_mask[] = {
	0x00,
	0x01,       0x03,       0x07,       0x0F,
	0x1F,       0x3F,       0x7F,       0xFF,
	0x1FF,      0x3FF,      0x7FF,      0xFFF,
	0x1FFF,     0x3FFF,     0x7FFF,     0xFFFF,
	0x1FFFF,    0x3FFFF,    0x7FFFF,    0xFFFFF,
	0x1FFFFF,   0x3FFFFF,   0x7FFFFF,   0xFFFFFF,
	0x1FFFFFF,  0x3FFFFFF,  0x7FFFFFF,  0xFFFFFFF,
	0x1FFFFFFF, 0x3FFFFFFF, 0x7FFFFFFF, 0xFFFFFFFF,
};

struct framebuffer {
	char *fp;                       /* pointer of framebuffer (read only) */
	char *buf;                      /* copy of framebuffer */
	int fd;                         /* file descriptor of framebuffer */
	int width, height;              /* display resolution */
	long screen_size;               /* screen data size (byte) */
	int line_length;                /* line length (byte) */
	int bpp;                        /* BYTES per pixel */
	struct fb_cmap *cmap,           /* cmap for legacy framebuffer (8bpp pseudocolor) */
		*cmap_org;                  /* copy of default cmap */
	struct fb_var_screeninfo vinfo; /* display info: need for get_color() */
		
};

struct image {
	unsigned char *data;
	int width;
	int height;
	int channel;
	bool alpha;
};

/* error functions */
void error(char *str)
{
	perror(str);
	exit(EXIT_FAILURE);
}

void fatal(char *str)
{
	fprintf(stderr, "%s\n", str);
	exit(EXIT_FAILURE);
}

/* wrapper of C functions */
int eopen(const char *path, int flag)
{
	int fd;
	errno = 0;

	if ((fd = open(path, flag)) < 0) {
		fprintf(stderr, "cannot open \"%s\"\n", path);
		error("open");
	}

	return fd;
}

void eclose(int fd)
{
	errno = 0;

	if (close(fd) < 0)
		error("close");
}

FILE *efopen(const char *path, char *mode)
{
	FILE *fp;
	errno = 0;

	if ((fp = fopen(path, mode)) == NULL) {
		fprintf(stderr, "cannot open \"%s\"\n", path);
		error("fopen");
	}

	return fp;
}

void efclose(FILE *fp)
{
	errno = 0;

	if (fclose(fp) < 0)
		error("fclose");
}

void *emmap(void *addr, size_t len, int prot, int flag, int fd, off_t offset)
{
	uint32_t *fp;
	errno = 0;

	if ((fp = (uint32_t *) mmap(addr, len, prot, flag, fd, offset)) == MAP_FAILED)
		error("mmap");

	return fp;
}

void emunmap(void *ptr, size_t len)
{
	errno = 0;

	if (munmap(ptr, len) < 0)
		error("munmap");
}

void *ecalloc(size_t size)
{
	void *p;
	errno = 0;

	if ((p = calloc(1, size)) == NULL)
		error("calloc");

	return p;
}

long int estrtol(const char *nptr, char **endptr, int base)
{
	long int ret;
	errno = 0;

	ret = strtol(nptr, endptr, base);
	if (ret == LONG_MIN || ret == LONG_MAX) {
		perror("strtol");
		return 0;
	}

	return ret;
}

/* framebuffer functions */
int str2num(char *str)
{
	if (str == NULL)
		return 0;

	return estrtol(str, NULL, 10);
}

void swap(int *a, int *b)
{
	int tmp = *a;
	*a  = *b;
	*b  = tmp;
}

uint32_t bit_reverse(uint32_t val, int bits)
{
    uint32_t ret = val;
    int shift = bits - 1;

    for (val >>= 1; val; val >>= 1) {
        ret <<= 1;
        ret |= val & 1;
        shift--;
    }

    return ret <<= shift;
}

void cmap_create(struct fb_cmap **cmap)
{
	*cmap           = (struct fb_cmap *) ecalloc(sizeof(struct fb_cmap));
	(*cmap)->start  = 0;
	(*cmap)->len    = CMAP_COLORS;
	(*cmap)->red    = (uint16_t *) ecalloc(sizeof(uint16_t) * CMAP_COLORS);
	(*cmap)->green  = (uint16_t *) ecalloc(sizeof(uint16_t) * CMAP_COLORS);
	(*cmap)->blue   = (uint16_t *) ecalloc(sizeof(uint16_t) * CMAP_COLORS);
	(*cmap)->transp = NULL;
}

void cmap_die(struct fb_cmap *cmap)
{
	if (cmap) {
		free(cmap->red);
		free(cmap->green);
		free(cmap->blue);
		free(cmap->transp);
		free(cmap);
	}
}

void cmap_init(struct framebuffer *fb, struct fb_var_screeninfo *vinfo)
{
	int i;
	uint8_t index;
	uint16_t r, g, b;

	if (ioctl(fb->fd, FBIOGETCMAP, fb->cmap_org) < 0) {
		/* not fatal, but we cannot restore original cmap */
		fprintf(stderr, "couldn't get original cmap\n");
		cmap_die(fb->cmap_org);
		fb->cmap_org = NULL; 
	}

	for (i = 0; i < CMAP_COLORS; i++) {
		/*
		in 8bpp mode, usually red and green have 3 bit, blue has 2 bit.
		so we will init cmap table like this.

		  index
		0000 0000 -> red [000] green [0 00] blue [00]
		...
		0110 1011 -> red [011] green [0 10] blue [11]
		...
		1111 1111 -> red [111] green [1 11] blue [11]

		these values are defined above:
		CMAP_RED_SHIFT   = 5
		CMAP_GREEN_SHIFT = 2
		CMAP_BLUE_SHIFT  = 0
		CMAP_RED_MASK    = 3
		CMAP_GREEN_MASK  = 3
		CMAP_BLUE_MASK   = 2
		*/
		index = (uint8_t) i;

		r = (index >> CMAP_RED_SHIFT)   & bit_mask[CMAP_RED_MASK];
		g = (index >> CMAP_GREEN_SHIFT) & bit_mask[CMAP_GREEN_MASK];
		b = (index >> CMAP_BLUE_SHIFT)  & bit_mask[CMAP_BLUE_MASK];

		/* cmap->{red,green,blue} has 16bit, so we should bulge */
		r = r * bit_mask[16] / bit_mask[CMAP_RED_MASK];
		g = g * bit_mask[16] / bit_mask[CMAP_GREEN_MASK];
		b = b * bit_mask[16] / bit_mask[CMAP_BLUE_MASK];

		/*
		if (DEBUG)
			fprintf(stderr, "index:%.2X r:%.4X g:%.4X b:%.4X\n", index, r, g, b);
		*/

		/* check bit reverse flag */
		*(fb->cmap->red + i) = (vinfo->red.msb_right) ?
			bit_reverse(r, 16) & bit_mask[16]: r;
		*(fb->cmap->green + i) = (vinfo->green.msb_right) ?
			bit_reverse(g, 16) & bit_mask[16]: g;
		*(fb->cmap->blue + i) = (vinfo->blue.msb_right) ?
			bit_reverse(b, 16) & bit_mask[16]: b;
	}

	if (ioctl(fb->fd, FBIOPUTCMAP, fb->cmap) < 0)
		fatal("ioctl: FBIOGET_VSCREENINFO failed");
}

void draw_cmap_table(struct framebuffer *fb)
{
	/* for debug */
	uint32_t c;
	int h, w, h_offset = 0, w_offset;
	enum {
		CELL_WIDTH = 8,
		CELL_HEIGHT = 16
	};

	for (c = 0; c < CMAP_COLORS; c++) {
		h_offset = (int) (c / 32) * CELL_HEIGHT;
		w_offset = (c % 32);

		for (h = 0; h < CELL_HEIGHT; h++) {
			for (w = 0; w < CELL_WIDTH; w++) {
				memcpy(fb->fp + (CELL_WIDTH * w_offset + w) * fb->bpp
					+ (h + h_offset) * fb->line_length, &c, fb->bpp);
			}
		}
	}
}

inline void get_rgb(uint8_t *r, uint8_t *g, uint8_t *b, unsigned char **data, int channel, bool has_alpha)
{
	if (channel <= 2) { /* grayscale (+ alpha) */
		*r = *g = *b = **data;
		*data += 1;
	}
	else { /* r, g, b (+ alpha) */
		*r = **data; *data += 1;
		*g = **data; *data += 1;
		*b = **data; *data += 1;
	}

	if (has_alpha)
		*data += 1;
}

inline uint32_t get_color(struct fb_var_screeninfo *vinfo, uint8_t r, uint8_t g, uint8_t b)
{
	if (vinfo->bits_per_pixel == 8) {
		/*
		calculate cmap color index:
		we only use following bits
			[011]0 0100: red
			[110]0 0110: green
			[01]01 1011: red
		*/
		r = (r >> (BITS_PER_BYTE - CMAP_RED_MASK))   & bit_mask[CMAP_RED_MASK];
		g = (g >> (BITS_PER_BYTE - CMAP_GREEN_MASK)) & bit_mask[CMAP_GREEN_MASK];
		b = (b >> (BITS_PER_BYTE - CMAP_BLUE_MASK))  & bit_mask[CMAP_BLUE_MASK];

		return (uint8_t) (r << CMAP_RED_SHIFT) | (g << CMAP_GREEN_SHIFT) | (b << CMAP_BLUE_SHIFT);
	}

	r = r >> (BITS_PER_BYTE - vinfo->red.length);
	g = g >> (BITS_PER_BYTE - vinfo->green.length);
	b = b >> (BITS_PER_BYTE - vinfo->blue.length);

	if (vinfo->red.msb_right)
		r = bit_reverse(r, vinfo->red.length)
			& bit_mask[vinfo->red.length];
	if (vinfo->green.msb_right)
		g = bit_reverse(g, vinfo->green.length)
			& bit_mask[vinfo->green.length];
	if (vinfo->blue.msb_right)
		b = bit_reverse(b, vinfo->blue.length)
			& bit_mask[vinfo->blue.length];

	return (r << vinfo->red.offset)
		+ (g << vinfo->green.offset)
		+ (b << vinfo->blue.offset);
}

void fb_init(struct framebuffer *fb)
{
	char *path;
	struct fb_fix_screeninfo finfo;
	struct fb_var_screeninfo vinfo;

	if ((path = getenv("FRAMEBUFFER")) != NULL)
		fb->fd = eopen(path, O_RDWR);
	else
		fb->fd = eopen(fb_path, O_RDWR);

	if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &finfo) < 0)
		fatal("ioctl: FBIOGET_FSCREENINFO failed");

	if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo) < 0)
		fatal("ioctl: FBIOGET_VSCREENINFO failed");

	/* check screen offset and initialize because linux console change this */
	/*
	if (vinfo.xoffset != 0 || vinfo.yoffset != 0) {
		vinfo.xoffset = vinfo.yoffset = 0;
		ioctl(fb->fd, FBIOPUT_VSCREENINFO, &vinfo);
	}
	*/

	fb->width = vinfo.xres;
	fb->height = vinfo.yres;
	fb->screen_size = finfo.smem_len;
	fb->line_length = finfo.line_length;

	if ((finfo.visual == FB_VISUAL_TRUECOLOR || finfo.visual == FB_VISUAL_DIRECTCOLOR)
		&& (vinfo.bits_per_pixel == 15 || vinfo.bits_per_pixel == 16
		|| vinfo.bits_per_pixel == 24 || vinfo.bits_per_pixel == 32)) {
		fb->cmap = fb->cmap_org = NULL;
		fb->bpp = (int) ceilf((float) vinfo.bits_per_pixel / BITS_PER_BYTE);
	}
	else if (finfo.visual == FB_VISUAL_PSEUDOCOLOR
		&& vinfo.bits_per_pixel == 8) {
		cmap_create(&fb->cmap);
		cmap_create(&fb->cmap_org);
		cmap_init(fb, &vinfo);
		fb->bpp = 1;
	}
	else /* non packed pixel, mono color, grayscale: not implimented */
		fatal("unsupported framebuffer type");

	fb->fp = (char *) emmap(0, fb->screen_size, PROT_WRITE | PROT_READ, MAP_SHARED, fb->fd, 0);
	fb->buf = (char *) ecalloc(fb->screen_size);
	fb->vinfo = vinfo;
}

void fb_die(struct framebuffer *fb)
{
	cmap_die(fb->cmap);
	if (fb->cmap_org) {
		//ioctl(fb->fd, FBIOPUTCMAP, fb->cmap_org); // not fatal
		cmap_die(fb->cmap_org);
	}
	free(fb->buf);
	emunmap(fb->fp, fb->screen_size);
	eclose(fb->fd);
}

/* libns{gif,bmp} functions */
void *gif_bitmap_create(int width, int height)
{
	return calloc(width * height, BYTES_PER_PIXEL);
}

void gif_bitmap_set_opaque(void *bitmap, bool opaque)
{
	(void) opaque;  /* unused */
	(void) bitmap;
}

bool gif_bitmap_test_opaque(void *bitmap)
{
	(void) bitmap;
	return false;
}

unsigned char *gif_bitmap_get_buffer(void *bitmap)
{
	return bitmap;
}

void gif_bitmap_destroy(void *bitmap)
{
	free(bitmap);
}

void gif_bitmap_modified(void *bitmap)
{
	(void) bitmap;
	return;
}

void *bmp_bitmap_create(int width, int height, unsigned int state)
{
	(void) state;  /* unused */
	return calloc(width * height, BYTES_PER_PIXEL);
}

unsigned char *bmp_bitmap_get_buffer(void *bitmap)
{
	return bitmap;
}

void bmp_bitmap_destroy(void *bitmap)
{
	free(bitmap);
}

size_t bmp_bitmap_get_bpp(void *bitmap)
{
	(void) bitmap;  /* unused */
	return BYTES_PER_PIXEL;
}

unsigned char *file_into_memory(const char *path, size_t *data_size)
{
	FILE *fd;
	struct stat sb;
	unsigned char *buffer;
	size_t size;
	size_t n;

	fd = fopen(path, "rb");
	if (!fd) {
		perror(path);
		exit(EXIT_FAILURE);
	}

	if (stat(path, &sb)) {
		perror(path);
		exit(EXIT_FAILURE);
	}
	size = sb.st_size;

	buffer = ecalloc(size);
	n = fread(buffer, 1, size, fd);
	if (n != size) {
		perror(path);
		exit(EXIT_FAILURE);
	}

	fclose(fd);
	*data_size = size;

	return buffer;
}

bool load_gif(const char *file, struct image *img)
{
	gif_bitmap_callback_vt gif_callbacks = {
		gif_bitmap_create,
		gif_bitmap_destroy,
		gif_bitmap_get_buffer,
		gif_bitmap_set_opaque,
		gif_bitmap_test_opaque,
		gif_bitmap_modified
	};
	size_t size;
	gif_result code;
	unsigned char *mem;
	gif_animation gif;

	gif_create(&gif, &gif_callbacks);
	mem = file_into_memory(file, &size);

	code = gif_initialise(&gif, size, mem);
	free(mem);
	if (code != GIF_OK && code != GIF_WORKING) {
		gif_finalise(&gif);
		return false;
	}

	code = gif_decode_frame(&gif, 0); /* read only first frame */
	if (code != GIF_OK) {
		gif_finalise(&gif);
		return false;
	}

	img->width   = gif.width;
	img->height  = gif.height;
	img->channel = BYTES_PER_PIXEL;

	size      = img->width * img->height * img->channel;
	img->data = (unsigned char *) ecalloc(size);
	memcpy(img->data, gif.frame_image, size);

	gif_finalise(&gif);

	return true;
}

bool load_bmp(const char *file, struct image *img)
{
	bmp_bitmap_callback_vt bmp_callbacks = {
		bmp_bitmap_create,
		bmp_bitmap_destroy,
		bmp_bitmap_get_buffer,
		bmp_bitmap_get_bpp
	};
	bmp_result code;
	size_t size;
	unsigned char *mem;
	bmp_image bmp;

	bmp_create(&bmp, &bmp_callbacks);
	mem = file_into_memory(file, &size);

	code = bmp_analyse(&bmp, size, mem);
	free(mem);
	if (code != BMP_OK) {
		bmp_finalise(&bmp);
		return false;
	}

	code = bmp_decode(&bmp);
	if (code != BMP_OK) {
		bmp_finalise(&bmp);
		return false;
	}

	img->width   = bmp.width;
	img->height  = bmp.height;
	img->channel = BYTES_PER_PIXEL;

	size      = img->width * img->height * img->channel;
	img->data = (unsigned char *) ecalloc(size);
	memcpy(img->data, bmp.bitmap, size);

	bmp_finalise(&bmp);

	return true;
}

/* libjpeg functions */
struct my_error_mgr {
	struct jpeg_error_mgr pub;
	jmp_buf setjmp_buffer;
};

void my_error_exit(j_common_ptr cinfo)
{
	struct my_error_mgr *myerr = (struct my_error_mgr *) cinfo->err;
	(*cinfo->err->output_message) (cinfo);
	longjmp(myerr->setjmp_buffer, 1);
}

bool load_jpeg(const char *file, struct image *img)
{
	int row_stride, size;
	FILE *fp;
	JSAMPARRAY buffer;
	struct jpeg_decompress_struct cinfo;
	struct my_error_mgr jerr;

	fp = efopen(file, "r");
	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = my_error_exit;

	if (setjmp(jerr.setjmp_buffer)) {
		jpeg_destroy_decompress(&cinfo);
		efclose(fp);
		return false;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, fp);
	jpeg_read_header(&cinfo, TRUE);

	cinfo.quantize_colors = FALSE;
	jpeg_start_decompress(&cinfo);

	img->width   = cinfo.output_width;
	img->height  = cinfo.output_height;
	img->channel = cinfo.output_components;

	size = img->width * img->height * img->channel;
	img->data = (unsigned char *) ecalloc(size);

	row_stride = cinfo.output_width * cinfo.output_components;
	buffer = (*cinfo.mem->alloc_sarray)((j_common_ptr) &cinfo, JPOOL_IMAGE, row_stride, 1);

	while (cinfo.output_scanline < cinfo.output_height) {
		jpeg_read_scanlines(&cinfo, buffer, 1);
		memcpy(img->data + (cinfo.output_scanline - 1) * row_stride, buffer[0], row_stride);
	}

	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	efclose(fp);

	return true;
}

/* libpng function */
bool load_png(const char *file, struct image *img)
{
	FILE *fp;
	int i, row_stride, size;
	png_bytep *row_pointers = NULL;
	unsigned char header[PNG_HEADER_SIZE];
	png_structp png_ptr;
	png_infop info_ptr;

	fp = efopen(file, "r");
	fread(header, 1, PNG_HEADER_SIZE, fp);

	if (png_sig_cmp(header, 0, PNG_HEADER_SIZE))
		return false;

	if ((png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL)) == NULL)
		return false;

	if ((info_ptr = png_create_info_struct(png_ptr)) == NULL) {
		png_destroy_read_struct(&png_ptr, (png_infopp) NULL, (png_infopp) NULL);
		return false;
	}

	if (setjmp(png_jmpbuf(png_ptr))) {
		png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
		fclose(fp);
		return false;
	}

	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, PNG_HEADER_SIZE);
	png_read_png(png_ptr, info_ptr,
		PNG_TRANSFORM_STRIP_ALPHA | PNG_TRANSFORM_EXPAND | PNG_TRANSFORM_STRIP_16, NULL);

	img->width   = png_get_image_width(png_ptr, info_ptr);
	img->height  = png_get_image_height(png_ptr, info_ptr);
	img->channel = png_get_channels(png_ptr, info_ptr);

	size = img->width * img->height * img->channel;
	img->data = (unsigned char *) ecalloc(size);

	row_stride   = png_get_rowbytes(png_ptr, info_ptr);
	row_pointers = png_get_rows(png_ptr, info_ptr);

	for (i = 0; i < img->height; i++)
		memcpy(img->data + row_stride * i, row_pointers[i], row_stride);

	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	fclose(fp);

	return true;
}

/* pnm functions */
inline int getint(FILE *fp)
{
	int c, n = 0;

	do {
		c = fgetc(fp);
	} while (isspace(c));

	while (isdigit(c)) {
		n = n * 10 + c - '0';
		c = fgetc(fp);
	}
	return n;
}

inline uint8_t pnm_normalize(int c, int type, int max_value)
{
	if (type == 1 || type == 4)
		return (c == 0) ? 0: 0xFF;
	else
		return 0xFF * c / max_value;
}

bool load_pnm(const char *file, struct image *img)
{
	int size, type, c, count, max_value = 0;
	FILE *fp;

	fp = efopen(file, "r");
	if (fgetc(fp) != 'P')
		return false;

	type = fgetc(fp) - '0';
	img->channel = (type == 1 || type == 2 || type == 4 || type == 5) ? 1:
		(type == 3 || type == 6) ? 3: -1;

	if (img->channel == -1)
		return false;

	/* read header */
	while ((c = fgetc(fp)) != EOF) {
		if (c == '#')
			while ((c = fgetc(fp)) != '\n');
		
		if (isspace(c))
			continue;

		if (isdigit(c)) {
			ungetc(c, fp);
			img->width  = getint(fp);
			img->height = getint(fp);
			if (type != 1 && type != 4)
				max_value = getint(fp);
			break;
		}
	}

	size = img->width * img->height * img->channel;
	img->data = ecalloc(size);

	/* read data */
	count = 0;
	if (1 <= type && type <= 3) {
		while ((c = fgetc(fp)) != EOF) {
			if (c == '#')
				while ((c = fgetc(fp)) != '\n');
			
			if (isspace(c))
				continue;

			if (isdigit(c)) {
				ungetc(c, fp);
				*(img->data + count++) = pnm_normalize(getint(fp), type, max_value);
			}
		}
	}
	else {
		while ((c = fgetc(fp)) != EOF)
			*(img->data + count++) = pnm_normalize(c, type, max_value);
	}

	efclose(fp);
	return true;
}

/* idump functions */
void usage()
{
	printf("idump [-h] [-f] [-r angle] image\n"
		"-h: show this help\n"
		"-f: fit image to display\n"
		"-r: rotate image (90/180/270)\n"
		);
}

void remove_temp_file()
{
	extern char temp_file[]; /* global */
	remove(temp_file);
}

char *make_temp_file(char *template)
{
	int fd;
	ssize_t size, file_size = 0;
	char buf[BUFSIZE];
	errno = 0;

	if ((fd = mkstemp(template)) < 0) {
		perror("mkstemp");
		return NULL;
	}
	if (DEBUG)
		fprintf(stderr, "tmp file:%s\n", template);

	/* register cleanup function */
	if (atexit(remove_temp_file) != 0)
		fatal("atexit() failed\nmaybe temporary file remains...\n");

	if (fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK) == -1)
		fprintf(stderr, "couldn't set O_NONBLOCK flag\n");

	while ((size = read(STDIN_FILENO, buf, BUFSIZE)) > 0) {
		write(fd, buf, size);
		file_size += size;
	}
	eclose(fd);

	if (file_size == 0) {
		fprintf(stderr, "stdin is empty\n");
		usage();
		exit(EXIT_FAILURE);
	}

	return template;
}

void load_image(const char *file, struct image *img)
{
	if ((strstr(file, "jpeg") || strstr(file, "jpg")) && load_jpeg(file, img))
		goto load_success;

	if (strstr(file, "png") && load_png(file, img))
		goto load_success;

	if (strstr(file, "bmp") && load_bmp(file, img))
		goto load_success;

	if (strstr(file, "gif") && load_gif(file, img))
		goto load_success;

	if ((strstr(file, "pnm") || strstr(file, "ppm") || strstr(file, "pgm") || strstr(file, "pbm"))
		&& load_pnm(file, img))
		goto load_success;

	if ((img->data = stbi_load(file, &img->width, &img->height,
		&img->channel, 0)) == NULL) {
		fprintf(stderr, "image load error: %s\n", file);
		exit(EXIT_FAILURE);
	}

load_success:
	img->alpha = (img->channel == 2 || img->channel == 4) ? true: false;
	if (DEBUG)
		fprintf(stderr, "image width:%d height:%d channel:%d\n",
			img->width, img->height, img->channel);
}

void free_image(struct image *img)
{
	free(img->data);
}

void rotate_image(struct image *img, int angle)
{
	int x1, x2, y1, y2, r, src_width;
	unsigned char *rotate_data, *dst, *src;
	long offset_dst, offset_src;

	static const int cos[3] = {0, -1,  0};
	static const int sin[3] = {1,  0, -1};

	int shift[3][3] = {
	/*   x_shift,         y_shift,        sign */
		{img->height - 1, 0              , -1},
		{img->width  - 1, img->height - 1,  1},
		{              0, img->width  - 1, -1}
	};

	if (angle != 90 && angle != 180 && angle != 270)
		return;
	/* r == 0: clockwise        : (angle 90)  */
	/* r == 1: upside down      : (angle 180) */
	/* r == 2: counter clockwise: (angle 270) */
	r = angle / 90 - 1;
	
	src_width = img->width;
	if (angle == 90 || angle == 270)
		swap(&img->width, &img->height);

	rotate_data = (unsigned char *) ecalloc(img->width * img->height * img->channel);

	src = img->data;
	dst = rotate_data;

	for (y2 = 0; y2 < img->height; y2++) {
		for (x2 = 0; x2 < img->width; x2++) {
			x1 = ((x2 - shift[r][0]) * cos[r] - (y2 - shift[r][1]) * sin[r]) * shift[r][2];
			y1 = ((x2 - shift[r][0]) * sin[r] + (y2 - shift[r][1]) * cos[r]) * shift[r][2];
			offset_src = img->channel * (y1 * src_width + x1);
			offset_dst = img->channel * (y2 * img->width + x2);
			memcpy(dst + offset_dst, src + offset_src, img->channel);
		}
	}

	free(img->data);
	img->data = rotate_data;

	if (DEBUG)
		fprintf(stderr, "rotated image: %dx%d size:%d\n",
			img->width, img->height, img->width * img->height * img->channel);
}

inline void get_average(struct image *img, int w_from, int w_to, int h_from, int h_to,
	int stride, unsigned char *pixel)
{
	int h, w, cells;
	unsigned char *ptr;
	uint8_t r, g, b;
	uint16_t rsum, gsum, bsum;

	rsum = gsum = bsum = 0;
	for (h = h_from; h < h_to; h++) {
		for (w = w_from; w < w_to; w++) {
			ptr = img->data + img->channel * (h * stride + w);
			get_rgb(&r, &g, &b, &ptr, img->channel, img->alpha);
			rsum += r;
			gsum += g;
			bsum += b;
		}
	}
	cells = (h_to - h_from) * (w_to - w_from);
	rsum /= cells;
	gsum /= cells;
	bsum /= cells;

	if (img->channel <= 2)
		*pixel++ = rsum;
	else {
		*pixel++ = rsum;
		*pixel++ = gsum;
		*pixel++ = bsum;
	}

	if (img->alpha)
		*pixel = 0;
}

void resize_image(struct image *img, int disp_width, int disp_height)
{
	int width_rate, height_rate, resize_rate;
	int w, h, src_width, h_from, w_from, h_to, w_to;
	unsigned char *dst, *resized_data, pixel[img->channel];
	long offset_dst;

	width_rate  = MULTIPLER * disp_width  / img->width;
	height_rate = MULTIPLER * disp_height / img->height;
	resize_rate = (width_rate < height_rate) ? width_rate: height_rate;

	if (DEBUG)
		fprintf(stderr, "width_rate:%.2d height_rate:%.2d resize_rate:%.2d\n",
			width_rate, height_rate, resize_rate);

	if ((resize_rate / MULTIPLER) > 1)
		return;

	src_width    = img->width;
	/* FIXME: let the same num (img->width == fb->width), if it causes SEGV, remove "+ 1" */
	img->width   = resize_rate * img->width / MULTIPLER + 1;
	img->height  = resize_rate * img->height / MULTIPLER;
	resized_data = (unsigned char *) ecalloc(img->width * img->height * img->channel);

	if (DEBUG)
		fprintf(stderr, "resized image: %dx%d size:%d\n",
			img->width, img->height, img->width * img->height * img->channel);

	dst = resized_data;

	for (h = 0; h < img->height; h++) {
		h_from = MULTIPLER * h / resize_rate;
		h_to   = MULTIPLER * (h + 1) / resize_rate;
		for (w = 0; w < img->width; w++) {
			w_from = MULTIPLER * w / resize_rate;
			w_to   = MULTIPLER * (w + 1) / resize_rate;

			get_average(img, w_from, w_to, h_from, h_to, src_width, pixel);
			offset_dst = img->channel * (h * img->width + w);
			memcpy(dst + offset_dst, pixel, img->channel);
		}
	}
	free(img->data);
	img->data = resized_data;
}

void draw_image(struct framebuffer *fb, struct image *img)
{
	/* TODO: check [xy]_offset (alyway zero now) */
	/* 14/06/08: offset removed */
	int w, h, offset, size;
	uint8_t r, g, b;
	uint32_t color;
	unsigned char *ptr;

	ptr = img->data;

	for (h = 0; h < img->height; h++) {
		for (w = 0; w < img->width; w++) {
			get_rgb(&r, &g, &b, &ptr, img->channel, img->alpha);
			color = get_color(&fb->vinfo, r, g, b);

			/* update copy buffer */
			if (w < fb->width && h < fb->height) {
				offset = w * fb->bpp + h * fb->line_length;
				memcpy(fb->buf + offset, &color, fb->bpp);
			}
		}
		/* draw each scanline */
		if (h < fb->height && img->width < fb->width) {
			offset = h * fb->line_length;
			size = img->width * fb->bpp;
			memcpy(fb->fp + offset, fb->buf + offset, size);
		}
	}
	/* we can draw all image data at once! */
	if (img->width >= fb->width) {
		offset = 0;
		size = (img->height > fb->height) ? fb->height: img->height;
		size *= fb->line_length; 
		memcpy(fb->fp + offset, fb->buf + offset, size);
	}
}


int main(int argc, char **argv)
{
	extern char temp_file[]; /* global */
	char *file;
	bool resize = false;
	int rotate = 0;
	int opt;
	struct framebuffer fb;
	struct image img;

	/* init */
	fb_init(&fb);

	/* check arg */
	while ((opt = getopt(argc, argv, "hfr:")) != -1) {
		switch (opt) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'f':
			resize = true;
			break;
		case 'r':
			rotate = str2num(optarg);
			break;
		default:
			break;
		}
	}

	/* open file */
	if (optind < argc)
		file = argv[optind];
	else
		file = make_temp_file(temp_file);

	load_image(file, &img);

	/* rotate/resize and draw */
	if (rotate != 0)
		rotate_image(&img, rotate);

	if (resize)
		resize_image(&img, fb.width, fb.height);

	draw_image(&fb, &img);

	/* release resource */
	free_image(&img);
	fb_die(&fb);

	return EXIT_SUCCESS;
}
