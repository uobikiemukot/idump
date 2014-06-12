/* See LICENSE for licence details. */
/* this header file depends loader.h */
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
		swapint(&img->width, &img->height);

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
	int row_stride, unsigned char *pixel)
{
	int h, w, cells;
	unsigned char *ptr;
	uint8_t r, g, b;
	uint16_t rsum, gsum, bsum;

	rsum = gsum = bsum = 0;
	for (h = h_from; h < h_to; h++) {
		for (w = w_from; w < w_to; w++) {
			ptr = img->data + img->channel * (h * row_stride + w);
			get_rgb(&r, &g, &b, &ptr, img->channel, img->alpha);
			rsum += r; gsum += g; bsum += b;
		}
	}
	cells = (h_to - h_from) * (w_to - w_from);
	/*
	printf("h_from:%d h_to:%d w_from:%d w_to:%d cells:%d\n",
		h_from, h_to, w_from, w_to, cells);
	*/
	if (cells > 1) {
		rsum /= cells; gsum /= cells; bsum /= cells;
	}

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
	/* TODO: support enlarge */
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

	if ((resize_rate / MULTIPLER) >= 1)
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

	for (h = 0; h < img->height; h++) {
		if (h >= fb->height)
			break;

		for (w = 0; w < img->width; w++) {
			if (w >= fb->width)
				break;

			ptr = img->data + img->channel * (h * img->width + w);
			get_rgb(&r, &g, &b, &ptr, img->channel, img->alpha);
			color = get_color(&fb->vinfo, r, g, b);

			/* update copy buffer */
			offset = h * fb->line_length + w * fb->bpp;
			memcpy(fb->buf + offset, &color, fb->bpp);
		}
		/* draw each scanline */
		if (img->width < fb->width) {
			offset = h * fb->line_length;
			size = img->width * fb->bpp;
			memcpy(fb->fp + offset, fb->buf + offset, size);
		}
	}
	/* we can draw all image data at once! */
	if (img->width >= fb->width) {
		size = (img->height > fb->height) ? fb->height: img->height;
		size *= fb->line_length;
		memcpy(fb->fp, fb->buf, size);
	}
}