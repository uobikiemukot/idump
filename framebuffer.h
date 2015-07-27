/* See LICENSE for licence details. */
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

struct framebuffer_t {
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

bool cmap_init(struct framebuffer_t *fb, struct fb_var_screeninfo *vinfo)
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

void draw_cmap_table(struct framebuffer_t *fb)
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

bool fb_init(struct framebuffer_t *fb, bool init_cmap)
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

void fb_die(struct framebuffer_t *fb)
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
