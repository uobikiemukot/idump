/* See LICENSE for licence details. */
#define _XOPEN_SOURCE 600
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fb.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
	VERBOSE   = false,
	BUFSIZE   = 1024,
	MULTIPLER = 1024,
};

/* error functions */
enum loglevel {
	DEBUG = 0,
	WARN,
	ERROR,
	FATAL,
};

void logging(int loglevel, char *format, ...)
{
	va_list arg;
	//FILE *outfp;

	static const char *loglevel2str[] = {
		[DEBUG] = "DEBUG",
		[WARN]  = "WARN",
		[ERROR] = "ERROR",
		[FATAL] = "FATAL",
	};

	if (loglevel == DEBUG && !VERBOSE)
		return;

	//outfp = (logfp == NULL) ? stderr: logfp;

	fprintf(stderr, ">>%s<<\t", loglevel2str[loglevel]);
	va_start(arg, format);
	vfprintf(stderr, format, arg);
	va_end(arg);
}

/* wrapper of C functions */
int eopen(const char *path, int flag)
{
	int fd;
	errno = 0;

	if ((fd = open(path, flag)) < 0) {
		logging(ERROR, "cannot open \"%s\"\n", path);
		logging(ERROR, "open: %s\n", strerror(errno));
	}
	return fd;
}

int eclose(int fd)
{
	int ret = 0;
	errno = 0;

	if ((ret = close(fd)) < 0)
		logging(ERROR, "close: %s\n", strerror(errno));

	return ret;
}

FILE *efopen(const char *path, char *mode)
{
	FILE *fp;
	errno = 0;

	if ((fp = fopen(path, mode)) == NULL) {
		logging(ERROR, "cannot open \"%s\"\n", path);
		logging(ERROR, "fopen: %s\n", strerror(errno));
	}
	return fp;
}

int efclose(FILE *fp)
{
	int ret;
	errno = 0;

	if ((ret = fclose(fp)) < 0)
		logging(ERROR, "fclose: %s\n", strerror(errno));

	return ret;
}

void *emmap(void *addr, size_t len, int prot, int flag, int fd, off_t offset)
{
	uint32_t *fp;
	errno = 0;

	if ((fp = (uint32_t *) mmap(addr, len, prot, flag, fd, offset)) == MAP_FAILED)
		logging(ERROR, "mmap: %s\n", strerror(errno));

	return fp;
}

int emunmap(void *ptr, size_t len)
{
	int ret;
	errno = 0;

	if ((ret = munmap(ptr, len)) < 0)
		logging(ERROR, "munmap: %s\n", strerror(errno));

	return ret;
}

void *ecalloc(size_t nmemb, size_t size)
{
	void *ptr;
	errno = 0;

	if ((ptr = calloc(nmemb, size)) == NULL)
		logging(ERROR, "calloc: %s\n", strerror(errno));

	return ptr;
}

long int estrtol(const char *nptr, char **endptr, int base)
{
	long int ret;
	errno = 0;

	ret = strtol(nptr, endptr, base);
	if (ret == LONG_MIN || ret == LONG_MAX) {
		logging(ERROR, "strtol: %s\n", strerror(errno));
		return 0;
	}

	return ret;
}

/* some useful functions */
int str2num(char *str)
{
	if (str == NULL)
		return 0;

	return estrtol(str, NULL, 10);
}

static inline void swapint(int *a, int *b)
{
	int tmp = *a;
	*a  = *b;
	*b  = tmp;
}

static inline int my_ceil(int val, int div)
{
	return (val + div - 1) / div;
}

static inline uint32_t bit_reverse(uint32_t val, int bits)
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

const char *fb_path = "/dev/fb0";

enum {
	BITS_PER_BYTE    = 8,
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
	uint8_t *fp;                    /* pointer of framebuffer (read only) */
	uint8_t *buf;                   /* copy of framebuffer */
	int fd;                         /* file descriptor of framebuffer */
	int width, height;              /* display resolution */
	long screen_size;               /* screen data size (byte) */
	int line_length;                /* line length (byte) */
	int bytes_per_pixel;            /* BYTES per pixel */
	struct fb_cmap *cmap,           /* cmap for legacy framebuffer (8bpp pseudocolor) */
		*cmap_org;                  /* copy of default cmap */
	struct fb_var_screeninfo vinfo; /* display info: need for get_color() */
};

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

bool cmap_create(struct fb_cmap **cmap)
{
	if ((*cmap = (struct fb_cmap *) ecalloc(1, sizeof(struct fb_cmap))) == NULL)
		return false;

	(*cmap)->start  = 0;
	(*cmap)->len    = CMAP_COLORS;

	(*cmap)->red    = (uint16_t *) ecalloc(CMAP_COLORS, sizeof(uint16_t));
	(*cmap)->green  = (uint16_t *) ecalloc(CMAP_COLORS, sizeof(uint16_t));
	(*cmap)->blue   = (uint16_t *) ecalloc(CMAP_COLORS, sizeof(uint16_t));
	(*cmap)->transp = NULL;

	if (!(*cmap)->red || !(*cmap)->green || !(*cmap)->blue) {
		cmap_die(*cmap);
		return false;
	}
	return true;
}

bool cmap_update(int fd, struct fb_cmap *cmap)
{
	if (!cmap) {
		logging(WARN, "cmap is NULL\n");
		return false;
	}

	if (ioctl(fd, FBIOPUTCMAP, cmap)) {
		logging(ERROR, "ioctl: FBIOPUTCMAP failed\n");
		return false;
	}
	return true;
}

bool cmap_init(struct framebuffer *fb, struct fb_var_screeninfo *vinfo)
{
	int i;
	uint8_t index;
	uint16_t r, g, b;

	if (ioctl(fb->fd, FBIOGETCMAP, fb->cmap_org)) {
		/* not fatal, but we cannot restore original cmap */
		logging(ERROR, "couldn't get original cmap\n");
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
		logging(DEBUG, "index:%.2X r:%.4X g:%.4X b:%.4X\n", index, r, g, b);
		*/

		/* check bit reverse flag */
		*(fb->cmap->red + i) = (vinfo->red.msb_right) ?
			bit_reverse(r, 16) & bit_mask[16]: r;
		*(fb->cmap->green + i) = (vinfo->green.msb_right) ?
			bit_reverse(g, 16) & bit_mask[16]: g;
		*(fb->cmap->blue + i) = (vinfo->blue.msb_right) ?
			bit_reverse(b, 16) & bit_mask[16]: b;
	}

	if (!cmap_update(fb->fd, fb->cmap))
		return false;

	return true;
}

void draw_cmap_table(struct framebuffer *fb)
{
	/* for debug */
	int y_offset = 0, x_offset;
	enum {
		CELL_WIDTH = 8,
		CELL_HEIGHT = 16
	};

	for (int c = 0; c < CMAP_COLORS; c++) {
		y_offset = (int) (c / 32) * CELL_HEIGHT;
		x_offset = (c % 32);

		for (int y = 0; y < CELL_HEIGHT; y++) {
			for (int x = 0; x < CELL_WIDTH; x++) {
				/* BUG: this memcpy is invalid, may fail at 24bpp and MSByte first env */
				memcpy(fb->fp + (CELL_WIDTH * x_offset + x) * fb->bytes_per_pixel
					+ (y + y_offset) * fb->line_length, &c, fb->bytes_per_pixel);
			}
		}
	}
}

static inline uint32_t get_color(struct fb_var_screeninfo *vinfo, uint8_t r, uint8_t g, uint8_t b)
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

bool fb_init(struct framebuffer *fb, bool init_cmap)
{
	char *path;
	struct fb_fix_screeninfo finfo;
	struct fb_var_screeninfo vinfo;

	const char *fb_type[] = {
		[FB_TYPE_PACKED_PIXELS]      = "FB_TYPE_PACKED_PIXELS",
		[FB_TYPE_PLANES]             = "FB_TYPE_PLANES",
		[FB_TYPE_INTERLEAVED_PLANES] = "FB_TYPE_INTERLEAVED_PLANES",
		[FB_TYPE_TEXT]               = "FB_TYPE_TEXT",
		[FB_TYPE_VGA_PLANES]         = "FB_TYPE_VGA_PLANES",
		[FB_TYPE_FOURCC]             = "FB_TYPE_FOURCC",
	};
	const char *fb_visual[] = {
		[FB_VISUAL_MONO01]             = "FB_VISUAL_MONO01",
		[FB_VISUAL_MONO10]             = "FB_VISUAL_MONO10",
		[FB_VISUAL_TRUECOLOR]          = "FB_VISUAL_TRUECOLOR",
		[FB_VISUAL_PSEUDOCOLOR]        = "FB_VISUAL_PSEUDOCOLOR",
		[FB_VISUAL_DIRECTCOLOR]        = "FB_VISUAL_DIRECTCOLOR",
		[FB_VISUAL_STATIC_PSEUDOCOLOR] = "FB_VISUAL_STATIC_PSEUDOCOLOR",
		[FB_VISUAL_FOURCC]             = "FB_VISUAL_FOURCC",
	};

	fb->fd = -1;
	fb->fp = MAP_FAILED;

	if ((path = getenv("FRAMEBUFFER")) != NULL)
		fb->fd = eopen(path, O_RDWR);
	else
		fb->fd = eopen(fb_path, O_RDWR);

	if (fb->fd < 0) {
		logging(ERROR, "couldn't open framebuffer device\n");
		return false;
	}

	if (ioctl(fb->fd, FBIOGET_FSCREENINFO, &finfo)) {
		logging(ERROR, "ioctl: FBIOGET_FSCREENINFO failed\n");
		goto err_init_failed;
	}

	if (ioctl(fb->fd, FBIOGET_VSCREENINFO, &vinfo)) {
		logging(ERROR, "ioctl: FBIOGET_VSCREENINFO failed\n");
		goto err_init_failed;
	}

	fb->width = vinfo.xres;
	fb->height = vinfo.yres;
	fb->screen_size = finfo.smem_len;
	fb->line_length = finfo.line_length;

	if (finfo.visual == FB_VISUAL_TRUECOLOR
		&& (vinfo.bits_per_pixel == 15 || vinfo.bits_per_pixel == 16
		|| vinfo.bits_per_pixel == 24 || vinfo.bits_per_pixel == 32)) {
		fb->cmap = fb->cmap_org = NULL;
		fb->bytes_per_pixel = my_ceil(vinfo.bits_per_pixel, BITS_PER_BYTE);
	} else if ((finfo.visual == FB_VISUAL_PSEUDOCOLOR || finfo.visual == FB_VISUAL_DIRECTCOLOR)
		&& vinfo.bits_per_pixel == 8) {
		/* XXX: direct color is not tested! */
		if (!init_cmap) {
			fb->cmap = fb->cmap_org = NULL;
		} else if (!cmap_create(&fb->cmap) || !cmap_create(&fb->cmap_org) || !cmap_init(fb, &vinfo)) {
			logging(ERROR, "cmap init failed\n");
			goto err_init_failed;
		}
		fb->bytes_per_pixel = 1;
	} else { /* non packed pixel, mono color, grayscale: not implimented */
		logging(ERROR, "unsupported framebuffer type\nvisual:%s type:%s bpp:%d\n",
			fb_visual[finfo.visual], fb_type[finfo.type], vinfo.bits_per_pixel);
		goto err_init_failed;
	}

	if ((fb->fp = (uint8_t *) emmap(0, fb->screen_size, PROT_WRITE, MAP_SHARED, fb->fd, 0)) == MAP_FAILED)
		goto err_init_failed;

	if ((fb->buf = (uint8_t *) ecalloc(1, fb->screen_size)) == NULL)
		goto err_init_failed;

	fb->vinfo = vinfo;
	return true;

err_init_failed:
	if (fb->fp != MAP_FAILED)
		munmap(fb->fp, fb->screen_size);
	if (fb->fd >= 0)
		close(fb->fd);
	return false;
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

/* for png */
#include "../lodepng.h"

/* for gif/bmp/(ico not supported) */
#include "../libnsgif.h"
#include "../libnsbmp.h"

#define STB_IMAGE_IMPLEMENTATION
/* to remove math.h dependency */
#define STBI_NO_HDR
#include "../stb_image.h"

enum {
	CHECK_HEADER_SIZE = 8,
	BYTES_PER_PIXEL   = 4,
	PNG_HEADER_SIZE   = 8,
	MAX_FRAME_NUM     = 128, /* limit of gif frames */
};

enum filetype_t {
	TYPE_JPEG,
	TYPE_PNG,
	TYPE_BMP,
	TYPE_GIF,
	TYPE_PNM,
	TYPE_UNKNOWN,
};

struct image {
	/* normally use data[0], data[n] (n > 1) for animanion gif */
	uint8_t *data[MAX_FRAME_NUM];
	int width;
	int height;
	int channel;
	bool alpha;
	/* for animation gif */
	int delay[MAX_FRAME_NUM];
	int frame_count; /* normally 1 */
	int loop_count;
	int current_frame; /* for yaimgfb */
};

unsigned char *file_into_memory(FILE *fp, size_t *data_size)
{
	unsigned char *buffer;
	size_t n, size;

	fseek(fp, 0L, SEEK_END);
	size = ftell(fp);

	if ((buffer = ecalloc(1, size)) == NULL)
		return NULL;

	fseek(fp, 0L, SEEK_SET);
	if ((n = fread(buffer, 1, size, fp)) != size) {
		free(buffer);
		return NULL;
	}
	*data_size = size;

	return buffer;
}

bool load_jpeg(FILE *fp, struct image *img)
{
	if ((img->data[0] = (uint8_t *) stbi_load_from_file(fp, &img->width, &img->height, &img->channel, 3)) == NULL)
		return false;

	return true;
}

bool load_png(FILE *fp, struct image *img)
{
	unsigned char *mem;
	size_t size;

	if ((mem = file_into_memory(fp, &size)) == NULL)
		return false;

	if (lodepng_decode24(&img->data[0], (unsigned *) &img->width, (unsigned *) &img->height, mem, size) != 0) {
		free(mem);
		return false;
	}

	img->channel = 3;
	free(mem);
	return true;
}

/* libns{gif,bmp} functions */
void *gif_bitmap_create(int width, int height)
{
	return calloc(width * height, BYTES_PER_PIXEL);
}

void gif_bitmap_set_opaque(void *bitmap, bool opaque)
{
	(void) opaque; /* unused */
	(void) bitmap;
}

bool gif_bitmap_test_opaque(void *bitmap)
{
	(void) bitmap; /* unused */
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
	(void) bitmap; /* unused */
	return;
}

bool load_gif(FILE *fp, struct image *img)
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
	int i;

	gif_create(&gif, &gif_callbacks);
	if ((mem = file_into_memory(fp, &size)) == NULL)
		return false;

	code = gif_initialise(&gif, size, mem);
	if (code != GIF_OK && code != GIF_WORKING)
		goto error_initialize_failed;

	img->width   = gif.width;
	img->height  = gif.height;
	img->channel = BYTES_PER_PIXEL; /* libnsgif always return 4bpp image */
	size = img->width * img->height * img->channel;

	/* read animation gif */
	img->frame_count = (gif.frame_count < MAX_FRAME_NUM) ? gif.frame_count: MAX_FRAME_NUM - 1;
	img->loop_count = gif.loop_count;

	for (i = 0; i < img->frame_count; i++) {
		code = gif_decode_frame(&gif, i);
		if (code != GIF_OK)
			goto error_decode_failed;

		if ((img->data[i] = (uint8_t *) ecalloc(1, size)) == NULL)
			goto error_decode_failed;
		memcpy(img->data[i], gif.frame_image, size);

		img->delay[i] = gif.frames[i].frame_delay;
	}

	gif_finalise(&gif);
	free(mem);
	return true;

error_decode_failed:
	img->frame_count = i;
	for (i = 0; i < img->frame_count; i++) {
		free(img->data[i]);
		img->data[i] = NULL;
	}
	gif_finalise(&gif);
error_initialize_failed:
	free(mem);
	return false;
}

void *bmp_bitmap_create(int width, int height, unsigned int state)
{
	(void) state; /* unused */
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
	(void) bitmap; /* unused */
	return BYTES_PER_PIXEL;
}

bool load_bmp(FILE *fp, struct image *img)
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
	if ((mem = file_into_memory(fp, &size)) == NULL)
		return false;

	code = bmp_analyse(&bmp, size, mem);
	if (code != BMP_OK)
		goto error_analyse_failed;

	code = bmp_decode(&bmp);
	if (code != BMP_OK)
		goto error_decode_failed;

	img->width   = bmp.width;
	img->height  = bmp.height;
	img->channel = BYTES_PER_PIXEL; /* libnsbmp always return 4bpp image */

	size = img->width * img->height * img->channel;
	if ((img->data[0] = (uint8_t *) ecalloc(1, size)) == NULL)
		goto error_decode_failed;
	memcpy(img->data[0], bmp.bitmap, size);

	bmp_finalise(&bmp);
	free(mem);
	return true;

error_decode_failed:
	bmp_finalise(&bmp);
error_analyse_failed:
	free(mem);
	return false;
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

bool load_pnm(FILE *fp, struct image *img)
{
	int size, type, c, count, max_value = 0;

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
	if ((img->data[0] = ecalloc(1, size)) == NULL)
		return false;

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
				*(img->data[0] + count++) = pnm_normalize(getint(fp), type, max_value);
			}
		}
	}
	else {
		while ((c = fgetc(fp)) != EOF)
			*(img->data[0] + count++) = pnm_normalize(c, type, max_value);
	}

	return true;
}

void init_image(struct image *img)
{
	for (int i = 0; i < MAX_FRAME_NUM; i++) {
		img->data[i] = NULL;
		img->delay[i] = 0;
	}
	img->width   = 0;
	img->height  = 0;
	img->channel = 0;
	img->alpha   = false;
	/* for animation gif */
	img->frame_count   = 1;
	img->loop_count    = 0;
	img->current_frame = 0;
}

void free_image(struct image *img)
{
	for (int i = 0; i < img->frame_count; i++) {
		free(img->data[i]);
		img->data[i] = NULL;
	}
}

enum filetype_t check_filetype(FILE *fp)
{
	/*
		JPEG(JFIF): FF D8
		PNG       : 89 50 4E 47 0D 0A 1A 0A (0x89 'P' 'N' 'G' '\r' '\n' 0x1A '\n')
		GIF       : 47 49 46 (ASCII 'G' 'I' 'F')
		BMP       : 42 4D (ASCII 'B' 'M')
		PNM       : 50 [31|32|33|34|35|36] ('P' ['1' - '6'])
	*/
	uint8_t header[CHECK_HEADER_SIZE];
	static uint8_t jpeg_header[] = {0xFF, 0xD8};
	static uint8_t png_header[]  = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
	static uint8_t gif_header[]  = {0x47, 0x49, 0x46};
	static uint8_t bmp_header[]  = {0x42, 0x4D};
	size_t size;

	if ((size = fread(header, 1, CHECK_HEADER_SIZE, fp)) != CHECK_HEADER_SIZE) {
		logging(ERROR, "couldn't read header\n");
		return TYPE_UNKNOWN;
	}
	fseek(fp, 0L, SEEK_SET);

	if (memcmp(header, jpeg_header, 2) == 0)
		return TYPE_JPEG;
	else if (memcmp(header, png_header, 8) == 0)
		return TYPE_PNG;
	else if (memcmp(header, gif_header, 3) == 0)
		return TYPE_GIF;
	else if (memcmp(header, bmp_header, 2) == 0)
		return TYPE_BMP;
	else if (header[0] == 'P' && ('0' <= header[1] && header[1] <= '6'))
		return TYPE_PNM;
	else
		return TYPE_UNKNOWN;
}

bool load_image(const char *file, struct image *img)
{
	int i;
	enum filetype_t type;
	FILE *fp;

	static bool (*loader[])(FILE *fp, struct image *img) = {
		[TYPE_JPEG] = load_jpeg,
		[TYPE_PNG]  = load_png,
		[TYPE_GIF]  = load_gif,
		[TYPE_BMP]  = load_bmp,
		[TYPE_PNM]  = load_pnm,
	};

	if ((fp = efopen(file, "r")) == NULL)
		return false;
 
 	if ((type = check_filetype(fp)) == TYPE_UNKNOWN) {
		logging(ERROR, "unknown file type: %s\n", file);
		goto image_load_error;
	}

	if (loader[type](fp, img)) {
		img->alpha = (img->channel == 2 || img->channel == 4) ? true: false;
		logging(DEBUG, "image width:%d height:%d channel:%d alpha:%s\n",
			img->width, img->height, img->channel, (img->alpha) ? "true": "false");
		if (img->frame_count > 1) {
			logging(DEBUG, "frame:%d loop:%d\n", img->frame_count, img->loop_count);
			for (i = 0; i < img->frame_count; i++)
				logging(DEBUG, "delay[%u]:%u\n", i, img->delay[i]);
		}
		efclose(fp);
		return true;
	}

image_load_error:
	logging(ERROR, "image load error: %s\n", file);
	//init_image(img);
	efclose(fp);
	return false;
}

/* this header file depends loader.h */

/* inline functions:
	never access member of struct image directly */
static inline int get_frame_count(struct image *img)
{
	return img->frame_count;
}

static inline uint8_t *get_current_frame(struct image *img)
{
	return img->data[img->current_frame];
}

static inline int get_current_delay(struct image *img)
{
	return img->delay[img->current_frame];
}

static inline void increment_frame(struct image *img)
{
	img->current_frame = (img->current_frame + 1) % img->frame_count;
}

static inline int get_image_width(struct image *img)
{
	return img->width;
}

static inline int get_image_height(struct image *img)
{
	return img->height;
}

static inline int get_image_channel(struct image *img)
{
	return img->channel;
}

static inline void get_rgb(struct image *img, uint8_t *data, int x, int y, uint8_t *r, uint8_t *g, uint8_t *b)
{
	uint8_t *ptr;

	ptr = data + img->channel * (y * img->width + x);

	if (img->channel <= 2) { /* grayscale (+ alpha) */
		*r = *g = *b = *ptr;
	} else {                 /* rgb (+ alpha) */
		*r = *ptr; *g = *(ptr + 1); *b = *(ptr + 2);
	}
}

static inline void get_average(struct image *img, uint8_t *data, int x_from, int y_from, int x_to, int y_to, uint8_t pixel[])
{
	int cell_num;
	uint8_t r, g, b;
	uint16_t rsum, gsum, bsum;

	rsum = gsum = bsum = 0;
	for (int y = y_from; y < y_to; y++) {
		for (int x = x_from; x < x_to; x++) {
			get_rgb(img, data, x, y, &r, &g, &b);
			rsum += r; gsum += g; bsum += b;
		}
	}

	cell_num = (y_to - y_from) * (x_to - x_from);
	if (cell_num > 1) {
		rsum /= cell_num; gsum /= cell_num; bsum /= cell_num;
	}

	if (img->channel <= 2)
		*pixel++ = rsum;
	else {
		*pixel++ = rsum; *pixel++ = gsum; *pixel++ = bsum;
	}

	if (img->alpha)
		*pixel = 0;
}

/* some image proccessing functions:
	never use *_single functions directly */
uint8_t *rotate_image_single(struct image *img, uint8_t *data, int angle)
{
	int x1, x2, y1, y2, r, dst_width, dst_height;
	uint8_t *rotated_data;
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
		return NULL;
	/* r == 0: clockwise        : (angle 90)  */
	/* r == 1: upside down      : (angle 180) */
	/* r == 2: counter clockwise: (angle 270) */
	r = angle / 90 - 1;
	
	if (angle == 90 || angle == 270) {
		dst_width  = img->height;
		dst_height = img->width;
	} else {
		dst_width  = img->width;
		dst_height = img->height;
	}

	if ((rotated_data = (uint8_t *) ecalloc(dst_width * dst_height, img->channel)) == NULL)
		return NULL;

	logging(DEBUG, "rotated image: %dx%d size:%d\n",
		dst_width, dst_height, dst_width * dst_height * img->channel);

	for (y2 = 0; y2 < dst_height; y2++) {
		for (x2 = 0; x2 < dst_width; x2++) {
			x1 = ((x2 - shift[r][0]) * cos[r] - (y2 - shift[r][1]) * sin[r]) * shift[r][2];
			y1 = ((x2 - shift[r][0]) * sin[r] + (y2 - shift[r][1]) * cos[r]) * shift[r][2];
			offset_src = img->channel * (y1 * img->width + x1);
			offset_dst = img->channel * (y2 * dst_width + x2);
			memcpy(rotated_data + offset_dst, data + offset_src, img->channel);
		}
	}
	free(data);

	img->width  = dst_width;
	img->height = dst_height;

	return rotated_data;
}

void rotate_image(struct image *img, int angle, bool rotate_all)
{
	uint8_t *rotated_data;

	if (rotate_all) {
		for (int i = 0; i < img->frame_count; i++)
			if ((rotated_data = rotate_image_single(img, img->data[i], angle)) != NULL)
				img->data[i] = rotated_data;
	} else {
		if ((rotated_data = rotate_image_single(img, img->data[img->current_frame], angle)) != NULL)
			img->data[img->current_frame] = rotated_data;
	}
}

uint8_t *resize_image_single(struct image *img, uint8_t *data, int disp_width, int disp_height)
{
	/* TODO: support enlarge */
	int width_rate, height_rate, resize_rate;
	int dst_width, dst_height, y_from, x_from, y_to, x_to;
	uint8_t *resized_data, pixel[img->channel];
	long offset_dst;

	width_rate  = MULTIPLER * disp_width  / img->width;
	height_rate = MULTIPLER * disp_height / img->height;
	resize_rate = (width_rate < height_rate) ? width_rate: height_rate;

	logging(DEBUG, "width_rate:%.2d height_rate:%.2d resize_rate:%.2d\n",
		width_rate, height_rate, resize_rate);

	/* only support shrink */
	if ((resize_rate / MULTIPLER) >= 1)
		return NULL;

	/* FIXME: let the same num (img->width == fb->width), if it causes SEGV, remove "+ 1" */
	dst_width  = resize_rate * img->width / MULTIPLER + 1;
	dst_height = resize_rate * img->height / MULTIPLER;

	if ((resized_data = (uint8_t *) ecalloc(dst_width * dst_height, img->channel)) == NULL)
		return NULL;

	logging(DEBUG, "resized image: %dx%d size:%d\n",
		dst_width, dst_height, dst_width * dst_height * img->channel);

	for (int y = 0; y < dst_height; y++) {
		y_from = MULTIPLER * y / resize_rate;
		y_to   = MULTIPLER * (y + 1) / resize_rate;
		for (int x = 0; x < dst_width; x++) {
			x_from = MULTIPLER * x / resize_rate;
			x_to   = MULTIPLER * (x + 1) / resize_rate;
			get_average(img, data, x_from, y_from, x_to, y_to, pixel);
			offset_dst = img->channel * (y * dst_width + x);
			memcpy(resized_data + offset_dst, pixel, img->channel);
		}
	}
	free(data);

	img->width  = dst_width;
	img->height = dst_height;

	return resized_data;
}

void resize_image(struct image *img, int disp_width, int disp_height, bool resize_all)
{
	uint8_t *resized_data;

	if (resize_all) {
		for (int i = 0; i < img->frame_count; i++)
			if ((resized_data = resize_image_single(img, img->data[i], disp_width, disp_height)) != NULL)
				img->data[i] = resized_data;
	} else {
		if ((resized_data = resize_image_single(img, img->data[img->current_frame], disp_width, disp_height)) != NULL)
			img->data[img->current_frame] = resized_data;
	}
}

uint8_t *normalize_bpp_single(struct image *img, uint8_t *data, int bytes_per_pixel)
{
	uint8_t *normalized_data, *src, *dst, r, g, b;

	if ((normalized_data = (uint8_t *)
		ecalloc(img->width * img->height, bytes_per_pixel)) == NULL)
		return NULL;

	if (img->channel <= 2) { /* grayscale (+ alpha) */
		for (int y = 0; y < img->height; y++) {
			for (int x = 0; x < img->width; x++) {
				src = data + img->channel * (y * img->width + x);
				dst = normalized_data + bytes_per_pixel * (y * img->width + x);
				*dst = *src; *(dst + 1) = *src; *(dst + 2) = *src;
			}
		}
	} else {                 /* rgb (+ alpha) */
		for (int y = 0; y < img->height; y++) {
			for (int x = 0; x < img->width; x++) {
				get_rgb(img, data, x, y, &r, &g, &b);
				dst = normalized_data + bytes_per_pixel * (y * img->width + x);
				*dst = r; *(dst + 1) = g; *(dst + 2) = b;
			}
		}
	}
	free(data);

	return normalized_data;
}

void normalize_bpp(struct image *img, int bytes_per_pixel, bool normalize_all)
{
	uint8_t *normalized_data;

	/* XXX: now only support bytes_per_pixel == 3 */
	if (bytes_per_pixel != 3)
		return;

	if (normalize_all) {
		for (int i = 0; i < img->frame_count; i++)
			if ((normalized_data = normalize_bpp_single(img, img->data[i], bytes_per_pixel)) != NULL)
				img->data[i] = normalized_data;
	} else {
		if ((normalized_data = normalize_bpp_single(img, img->data[img->current_frame], bytes_per_pixel)) != NULL)
			img->data[img->current_frame] = normalized_data;
	}
}

void draw_single(struct framebuffer *fb, struct image *img, uint8_t *data,
	int offset_x, int offset_y, int shift_x, int shift_y, int width, int height)
{
	int offset, size;
	uint8_t r, g, b;
	uint32_t color;

	for (int y = 0; y < height; y++) {
		if (y >= fb->height)
			break;

		for (int x = 0; x < width; x++) {
			if (x >= fb->width)
				break;

			get_rgb(img, data, x + shift_x, y + shift_y, &r, &g, &b);
			color = get_color(&fb->vinfo, r, g, b);

			/* update copy buffer */
			offset = (y + offset_y) * fb->line_length + (x + offset_x) * fb->bytes_per_pixel;
			memcpy(fb->buf + offset, &color, fb->bytes_per_pixel);
		}
		/* draw each scanline */
		if (width < fb->width) {
			offset = (y + offset_y) * fb->line_length + offset_x * fb->bytes_per_pixel;
			size = width * fb->bytes_per_pixel;
			memcpy(fb->fp + offset, fb->buf + offset, size);
		}
	}
	/* we can draw all image data at once! */
	if (width >= fb->width) {
		size = (height > fb->height) ? fb->height: height;
		size *= fb->line_length;
		memcpy(fb->fp, fb->buf, size);
	}
}

void draw_image(struct framebuffer *fb, struct image *img,
	int offset_x, int offset_y, int shift_x, int shift_y, int width, int height, bool enable_anim)
{
	/*
		+- screen -----------------+
		|        ^                 |
		|        | offset_y        |
		|        v                 |
		|        +- image --+      |
		|<------>|          |      |
		|offset_x|          |      |
		|        |          |      |
		|        +----------+      |
		+--------------|-----------+
					   |
					   v
		+- image ----------------------+
		|       ^                      |
		|       | shift_y              |
		|       v                      |
		|       +- view port + ^       |
		|<----->|            | |       |
		|shift_x|            | | height|
		|       |            | |       |
		|       +------------+ v       |
		|       <-  width ->           |
		+------------------------------+
	*/
	int loop_count = 0;

	if (shift_x + width > img->width)
		width = img->width - shift_x;

	if (shift_y + height > img->height)
		height = img->height - shift_y;

	if (offset_x + width > fb->width)
		width = fb->width - offset_x;

	if (offset_y + height > fb->height)
		height = fb->height - offset_y;

	/* XXX: ignore img->loop_count, force 1 loop */
	if (enable_anim) {
		while (loop_count < img->frame_count) {
			draw_single(fb, img, img->data[loop_count], offset_x, offset_y, shift_x, shift_y, width, height);
			usleep(img->delay[loop_count] * 10000); /* gif delay 1 == 1/100 sec */
			loop_count++;
		}
	} else {
		draw_single(fb, img, img->data[img->current_frame], offset_x, offset_y, shift_x, shift_y, width, height);
	}
}

char temp_file[] = "/tmp/idump.XXXXXX";

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
	logging(DEBUG, "tmp file:%s\n", template);

	/* register cleanup function */
	if (atexit(remove_temp_file) != 0)
		logging(ERROR, "atexit() failed\nmaybe temporary file remains...\n");

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
		return NULL;
	}

	return template;
}

int main(int argc, char **argv)
{
	extern char temp_file[]; /* global */
	char *file;
	bool resize = false;
	int angle = 0;
	int opt;
	struct framebuffer fb;
	struct image img;

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
			angle = str2num(optarg);
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

	if (file == NULL) {
		logging(FATAL, "input file not found\n");
		return EXIT_FAILURE;
	}

	/* init */
	if (!fb_init(&fb, true)) {
		logging(FATAL, "fb_init() failed\n");
		return EXIT_FAILURE;
	}

	init_image(&img);

	if (!load_image(file, &img))
		goto cleanup;

	/* rotate/resize and draw */
	/* TODO: support color reduction for 8bpp mode */
	if (angle != 0)
		rotate_image(&img, angle, true);

	if (resize)
		resize_image(&img, fb.width, fb.height, true);

	draw_image(&fb, &img, 0, 0, 0, 0, img.width, img.height, true);

	/* cleanup resource */
cleanup:
	free_image(&img);
	fb_die(&fb);

	return EXIT_SUCCESS;
}
