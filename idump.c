#define _XOPEN_SOURCE 600
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fb.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

const char *fb_path = "/dev/fb0";
char temp_file[] = "/tmp/idump.XXXXXX";

enum {
	DEBUG            = false,
	BITS_PER_BYTE    = 8,
	BUFSIZE          = 1024,
	MULTIPLER        = 1024,
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

/* some functions for Linux framebuffer */
int str2num(char *str)
{
	if (str == NULL)
		return 0;

	return estrtol(str, NULL, 10);
}

int my_ceil(float val, float div)
{
	float ret; 

	ret = val / div;
	if ((int) ret < ret)
		return (int) (ret + 1);
	else
		return (int) ret;
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
		in 8bpp mode, usually red and green have 3bit, blue has 1bit.
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

void get_rgb(uint8_t *r, uint8_t *g, uint8_t *b, unsigned char **data, int channel)
{
	if (channel == 1 || channel == 2) { /* grayscale or grayscale + alpha */
		*r = *g = *b = **data;
		*data += 1;
	}
	else { /* channel == 3: r, g, b or channel == 3: r, g, b + alpha */
		*r = **data; *data += 1;
		*g = **data; *data += 1;
		*b = **data; *data += 1;
	}

	if (channel == 2 || channel == 4) /* has alpha */
		*data += 1;
}

uint32_t get_color(struct fb_var_screeninfo *vinfo, uint8_t r, uint8_t g, uint8_t b)
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
		fb->bpp = (int) my_ceil(vinfo.bits_per_pixel, BITS_PER_BYTE);
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

/* idump functions */
void usage()
{
	printf("idump [-h] [-f] [-r angle] image\n"
		"-h: show this help\n"
		"-f: fit image to display\n"
		"-r: rotate image (90/180/270)"
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
		exit(EXIT_SUCCESS);
	}

	return template;
}

void load_image(char *file, struct image *img)
{
	if ((img->data = stbi_load(file, &img->width, &img->height,
		&img->channel, 0)) == NULL) {
		fprintf(stderr, "image load error: %s\n", file);
		exit(EXIT_FAILURE);
	}

	if (DEBUG)
		fprintf(stderr, "image width:%d height:%d channel:%d\n",
			img->width, img->height, img->channel);
}

void free_image(struct image *img)
{
	stbi_image_free(img->data);
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

void resize_image( struct image *img, int disp_width, int disp_height)
{
	int width_rate, height_rate, resize_rate;
	int w, h, src_width, h_step, w_step;
	unsigned char *src, *dst, *resized_data; //*resized_width, *resized_height;
	long offset_dst, offset_src;

	width_rate  = MULTIPLER * disp_width  / img->width;
	height_rate = MULTIPLER * disp_height / img->height;
	resize_rate = (width_rate < height_rate) ? width_rate: height_rate;

	if (DEBUG)
		fprintf(stderr, "width_rate:%.2d height_rate:%.2d resize_rate:%.2d\n",
			width_rate, height_rate, resize_rate);

	if ((resize_rate / MULTIPLER) > 1)
		return;

	src_width    = img->width;
	img->width   = resize_rate * img->width / MULTIPLER;
	img->height  = resize_rate * img->height / MULTIPLER;
	resized_data = (unsigned char *) ecalloc(img->width * img->height * img->channel);

	src = img->data;
	dst = resized_data;

	for (h = 0; h < img->height; h++) {
		h_step = MULTIPLER * h / resize_rate;
		for (w = 0; w < img->width; w++) {
			w_step = MULTIPLER * w / resize_rate;
			offset_src = img->channel * (h_step * src_width + w_step);
			offset_dst = img->channel * (h * img->width + w);
			memcpy(dst + offset_dst, src + offset_src, img->channel);
		}
	}

	free(img->data);
	img->data = resized_data;

	if (DEBUG)
		fprintf(stderr, "resized image: %dx%d size:%d\n",
			img->width, img->height, img->width * img->height * img->channel);
}

void draw_image(struct framebuffer *fb, struct image *img, int x_offset, int y_offset)
{
	/* TODO: check [xy]_offset (alyway zero now) */
	int w, h, offset, size;
	uint8_t r, g, b;
	uint32_t color;
	unsigned char *ptr;

	ptr = img->data;

	if (img->channel < 3) {
		fprintf(stderr, "grayscale image not suppurted\n");
		return;
	}

	for (h = 0; h < img->height; h++) {
		for (w = 0; w < img->width; w++) {
			get_rgb(&r, &g, &b, &ptr, img->channel);
			color = get_color(&fb->vinfo, r, g, b);

			if (w >= fb->width || h >= fb->height)
				continue;

			/* update copy buffer */
			offset = (w + x_offset) * fb->bpp + (h + y_offset) * fb->line_length;
			memcpy(fb->buf + offset, &color, fb->bpp);
		}
		/* draw each scanline */
		if (img->width < fb->width) {
			offset = x_offset * fb->bpp + (h + y_offset) * fb->line_length;
			size = img->width * fb->bpp;
			memcpy(fb->fp + offset, fb->buf + offset, size);
		}
	}
	/* we can draw all image data at once! */
	if (img->width >= fb->width) {
		offset = x_offset * fb->bpp + y_offset * fb->line_length;
		size = (img->height >= fb->height) ? fb->height: img->height;
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
			break;
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

	draw_image(&fb, &img, 0, 0);

	/* release resource */
	free_image(&img);
	fb_die(&fb);

	return EXIT_SUCCESS;
}
