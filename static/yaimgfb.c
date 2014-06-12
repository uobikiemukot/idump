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
#include "../lib/libnsgif.h"
#include "../lib/libnsbmp.h"

/* for png */
#include "../lib/lodepng.h"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_HDR
#include "../lib/stb_image.h"

const char *fb_path = "/dev/fb0";

enum {
	DEBUG            = true,
	BITS_PER_BYTE    = 8,
	BUFSIZE          = 1024,
	MULTIPLER        = 1024,
	BYTES_PER_PIXEL  = 4,
	MAX_ARGS         = 128,
	MAX_IMAGE        = 1024,
	PNG_HEADER_SIZE  = 8,
	CMAP_COLORS      = 256,
	CMAP_RED_SHIFT   = 5,
	CMAP_GREEN_SHIFT = 2,
	CMAP_BLUE_SHIFT  = 0,
	CMAP_RED_MASK    = 3,
	CMAP_GREEN_MASK  = 3,
	CMAP_BLUE_MASK   = 2
};

enum w3m_op {
	W3M_DRAW = 0,
	W3M_REDRAW,
	W3M_STOP,
	W3M_SYNC,
	W3M_NOP,
	W3M_GETSIZE,
	W3M_CLEAR,
	NUM_OF_W3M_FUNC,
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

struct parm_t { /* for parse_arg() */
    int argc;
    char *argv[MAX_ARGS];
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

void swapint(int *a, int *b)
{
	int tmp = *a;
	*a  = *b;
	*b  = tmp;
}

inline int my_ceil(int val, int div)
{
	return (val + div - 1) / div;
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
		fb->bpp = my_ceil(vinfo.bits_per_pixel, BITS_PER_BYTE);
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
		fprintf(stderr, "gif_initialize() failed\n");
		gif_finalise(&gif);
		return false;
	}

	code = gif_decode_frame(&gif, 0); /* read only first frame */
	if (code != GIF_OK) {
		fprintf(stderr, "gif_decode_frame() failed\n");
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

/* parse_arg functions */
void reset_parm(struct parm_t *pt)
{
	int i;

	pt->argc = 0;
	for (i = 0; i < MAX_ARGS; i++)
		pt->argv[i] = NULL;
}

void add_parm(struct parm_t *pt, char *cp)
{
	if (pt->argc >= MAX_ARGS)
		return;

	if (DEBUG)
		fprintf(stderr, "argv[%d]: %s\n",
			pt->argc, (cp == NULL) ? "NULL": cp);

	pt->argv[pt->argc] = cp;
	pt->argc++;
}

void parse_arg(char *buf, struct parm_t *pt, int delim, int (is_valid)(int c))
{
    /*
        v..........v d           v.....v d v.....v ... d
        (valid char) (delimiter)
        argv[0]                  argv[1]   argv[2] ...   argv[argc - 1]
    */
	int i, length;
	char *cp, *vp;

	if (buf == NULL)
		return;

	length = strlen(buf);
	if (DEBUG)
		fprintf(stderr, "parse_arg()\nlength:%d\n", length);

	vp = NULL;
	for (i = 0; i < length; i++) {
		cp = &buf[i];

		if (vp == NULL && is_valid(*cp))
			vp = cp;

		if (*cp == delim) {
			*cp = '\0';
			add_parm(pt, vp);
			vp = NULL;
		}

		if (i == (length - 1) && (vp != NULL || *cp == '\0'))
			add_parm(pt, vp);
	}

	if (DEBUG)
		fprintf(stderr, "argc:%d\n", pt->argc);
}

/* idump functions */
void free_image(struct image *img)
{
	if (img->data != NULL)
		free(img->data);
	img->data = NULL;
}

void load_image(const char *file, struct image *img)
{
	free_image(img);

	if (strstr(file, "png") &&
		lodepng_decode24_file(&img->data,
		(unsigned *) &img->width, (unsigned *) &img->height, file) == 0) {
		img->channel = 3;
		goto load_success;
	}

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

void init_images(struct image img[])
{
	int i;

	for (i = 0; i < MAX_IMAGE; i++) {
		img[i].data    = NULL;
		img[i].width   = 0;
		img[i].height  = 0;
		img[i].channel = 0;
		img[i].alpha   = false;
	}
}

void clear_images(struct image img[])
{
	int i;

	for (i = 0; i < MAX_IMAGE; i++) {
		free_image(&img[i]);

		img[i].width   = 0;
		img[i].height  = 0;
		img[i].channel = 0;
		img[i].alpha   = false;
	}
}

void w3m_draw(struct framebuffer *fb, struct image img[], struct parm_t *parm, int op)
{
	int index, offset_x, offset_y, width, height, sx, sy, sw, sh;
	int w, h, offset_fb, offset_img, img_stride;
	char *file;
	unsigned char *ptr;
	uint8_t r, g, b;
	uint32_t color;
	struct image *ip;

	if (DEBUG)
		fprintf(stderr, "w3m_%s()\n", (op == W3M_DRAW) ? "draw": "redraw");

	if (parm->argc != 11)
		return;

	index     = str2num(parm->argv[1]);
	offset_x  = str2num(parm->argv[2]);
	offset_y  = str2num(parm->argv[3]);
	width     = str2num(parm->argv[4]);
	height    = str2num(parm->argv[5]);
	sx        = str2num(parm->argv[6]);
	sy        = str2num(parm->argv[7]);
	sw        = str2num(parm->argv[8]);
	sh        = str2num(parm->argv[9]);
	file      = parm->argv[10];

	(void) sw;
	(void) sh;

	index = (index < 0) ? 0:
		(index >= MAX_IMAGE) ? MAX_IMAGE - 1: index;
	ip = &img[index];

	if (op == W3M_DRAW)
		load_image(file, ip);
	else if (ip->data == NULL)
		return;

	if (sx + width > ip->width)
		width = ip->width - sx;
	
	if (sy + height > ip->height)
		height = ip->height - sy;

	if (offset_x + width > fb->width)
		width = fb->width - offset_x;

	if (offset_y + height > fb->height)
		height = fb->height - offset_y;

	img_stride = ip->width * ip->channel;
	offset_fb  = offset_y * fb->line_length + offset_x * fb->bpp;
	offset_img = sy * img_stride + sx * ip->channel;

	for (h = 0; h < height; h++) {
		for (w = 0; w < width; w++) {
			ptr = ip->data + offset_img + w * ip->channel;
			get_rgb(&r, &g, &b, &ptr, ip->channel, ip->alpha);
			color = get_color(&fb->vinfo, r, g, b);
			memcpy(fb->buf + offset_fb + w * fb->bpp, &color, fb->bpp);
		}
		memcpy(fb->fp + offset_fb, fb->buf + offset_fb, width * fb->bpp);
		offset_fb  += fb->line_length;
		offset_img += img_stride;
	}
}

void w3m_stop(struct framebuffer *fb, struct image img[], struct parm_t *parm, int op)
{
	if (DEBUG)
		fprintf(stderr, "w3m_stop()\n");

	(void) op;
	(void) fb;
	(void) img;
	(void) parm;
}

void w3m_sync(struct framebuffer *fb, struct image img[], struct parm_t *parm, int op)
{
	if (DEBUG)
		fprintf(stderr, "w3m_sync()\n");

	(void) op;
	(void) fb;
	(void) img;
	(void) parm;
}

void w3m_nop(struct framebuffer *fb, struct image img[], struct parm_t *parm, int op)
{
	if (DEBUG)
		fprintf(stderr, "w3m_nop()\n");

	(void) op;
	(void) fb;
	(void) img;
	(void) parm;
	printf("\n");
}

void w3m_getsize(struct framebuffer *fb, struct image img[], struct parm_t *parm, int op)
{
	char *file;

	if (DEBUG)
		fprintf(stderr, "w3m_getsize()\n");

	(void) op;
	(void) fb;

	if (parm->argc != 2) 
		return;

	file = parm->argv[1];

	load_image(file, &img[0]);
	printf("%d %d\n", img[0].width, img[0].height);
}

void w3m_clear(struct framebuffer *fb, struct image img[], struct parm_t *parm, int op)
{
	int offset_x, offset_y, width, height;

	if (DEBUG)
		fprintf(stderr, "w3m_clear()\n");

	(void) op;
	(void) fb;
	(void) img;

	if (parm->argc != 5)
		return;

	offset_x  = str2num(parm->argv[1]);
	offset_y  = str2num(parm->argv[2]);
	width     = str2num(parm->argv[3]);
	height    = str2num(parm->argv[4]);

	(void) offset_x;
	(void) offset_y;
	(void) width;
	(void) height;

	//clear_images(img);
}

void (*w3m_func[NUM_OF_W3M_FUNC])(struct framebuffer *fb, struct image img[], struct parm_t *parm, int op) = {
	[W3M_DRAW]    = w3m_draw,
	[W3M_REDRAW]  = w3m_draw,
	[W3M_STOP]    = w3m_stop,
	[W3M_SYNC]    = w3m_sync,
	[W3M_NOP]     = w3m_nop,
	[W3M_GETSIZE] = w3m_getsize,
	[W3M_CLEAR]   = w3m_clear,
};

int main(int argc, char *argv[])
{
	/*
	command line option
	  -bg    : background color (for transparent image?)
	  -x     : image position offset x
	  -y     : image position offset x
	  -test  : request display size (response "width height\n")
	  -size  : request image size  (response "width height\n")
	  -anim  : number of max frame of animation image?
	  -margin: margin of clear region?
	  -debug : debug flag (not used)
	*/
	/*
	w3mimg protocol
	  0  1  2 ....
	 +--+--+--+--+ ...... +--+--+
	 |op|; |args             |\n|
	 +--+--+--+--+ .......+--+--+

	 args is separeted by ';'
	 op   args
	  0;  params    draw image
	  1;  params    redraw image
	  2;  -none-    terminate drawing
	  3;  -none-    sync drawing
	  4;  -none-    nop, sync communication
	                response '\n'
	  5;  path      get size of image,
	                response "<width> <height>\n"
	  6;  params(6) clear image

	  params
	   <n>;<x>;<y>;<w>;<h>;<sx>;<sy>;<sw>;<sh>;<path>
	  params(6)
	   <x>;<y>;<w>;<h>
	*/
	int i, op, optind, length;
	char buf[BUFSIZE];
	struct framebuffer fb;
	struct image img[MAX_IMAGE];
	struct parm_t parm;

	freopen("/tmp/w3mimg.log", "a", stderr);

	if (DEBUG) {
		fprintf(stderr, "---\ncommand line\n");
		for (i = 0; i < argc; i++)
			fprintf(stderr, "argv[%d]:%s\n", i, argv[i]);
		fprintf(stderr, "argc:%d\n", argc);
	}

	fb_init(&fb);
	init_images(img);

	optind = 1;
	while (optind < argc) {
		if (strncmp(argv[optind], "-test", 5) == 0) {
			printf("%d %d\n", fb.width, fb.height);
			return EXIT_SUCCESS;
		}
		else if (strncmp(argv[optind], "-size", 5) == 0 && ++optind < argc) {
			load_image(argv[optind], &img[0]);
			printf("%d %d\n", img[0].width, img[0].height);
			return EXIT_SUCCESS;
		}
		optind++;
	}

    while (fgets(buf, BUFSIZE, stdin) != NULL) {
		length = strlen(buf);
		buf[length - 1] = '\0'; /* chomp '\n' */

		if (DEBUG)
			fprintf(stderr, "stdin: %s\n", buf);

		reset_parm(&parm);
		parse_arg(buf, &parm, ';', isgraph);

		if (parm.argc < 0)
			continue;

		op = str2num(parm.argv[0]);
		if (0 <= op && op < NUM_OF_W3M_FUNC)
			w3m_func[op](&fb, img, &parm, op);

		fflush(stdout);
		fflush(stderr);
    }

	clear_images(img);
	fb_die(&fb);

	return EXIT_SUCCESS;
}
