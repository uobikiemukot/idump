/* See LICENSE for licence details. */
#include "yaimgfb.h"
#include "../util.h"
#include "../parsearg.h"
#include "../framebuffer.h"
#include "../loader.h"
#include "../image.h"

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

	if (op == W3M_DRAW) {
		free_image(ip);
		if (load_image(file, ip) == false)
			return;
	}
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
			get_rgb(&r, &g, &b, ptr, ip->channel);
			color = get_color(&fb->vinfo, r, g, b);
			memcpy(fb->buf + offset_fb + w * fb->bpp, &color, fb->bpp);
		}
		memcpy(fb->fp + offset_fb, fb->buf + offset_fb, width * fb->bpp);
		offset_fb  += fb->line_length;
		offset_img += img_stride;
	}
}

void w3m_stop()
{
	if (DEBUG)
		fprintf(stderr, "w3m_stop()\n");
}

void w3m_sync()
{
	if (DEBUG)
		fprintf(stderr, "w3m_sync()\n");
}

void w3m_nop()
{
	if (DEBUG)
		fprintf(stderr, "w3m_nop()\n");

	printf("\n");
}

void w3m_getsize(struct image *img, const char *file)
{
	if (DEBUG)
		fprintf(stderr, "w3m_getsize()\n");

	/*
	if (parm->argc != 2) 
		return;
	*/

	free_image(img);
	if (load_image(file, img))
		printf("%d %d\n", img->width, img->height);
	else
		printf("0 0\n");
}

void w3m_clear(struct image img[], struct parm_t *parm)
{
	int offset_x, offset_y, width, height;

	if (DEBUG)
		fprintf(stderr, "w3m_clear()\n");

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

	(void) img;
	//clear_images(img);
}

/*
void (*w3m_func[NUM_OF_W3M_FUNC])(struct framebuffer *fb, struct image img[], struct parm_t *parm, int op) = {
	[W3M_DRAW]    = w3m_draw,
	[W3M_REDRAW]  = w3m_draw,
	[W3M_STOP]    = w3m_stop,
	[W3M_SYNC]    = w3m_sync,
	[W3M_NOP]     = w3m_nop,
	[W3M_GETSIZE] = w3m_getsize,
	[W3M_CLEAR]   = w3m_clear,
};
*/

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

	freopen("/tmp/w3mimg.log", "w", stderr);

	if (DEBUG) {
		fprintf(stderr, "---\ncommand line\n");
		for (i = 0; i < argc; i++)
			fprintf(stderr, "argv[%d]:%s\n", i, argv[i]);
		fprintf(stderr, "argc:%d\n", argc);
	}

	/* init */
	fb_init(&fb);
	for (i = 0; i < MAX_IMAGE; i++)
		init_image(&img[i]);

	/* check args */
	optind = 1;
	while (optind < argc) {
		if (strncmp(argv[optind], "-test", 5) == 0) {
			printf("%d %d\n", fb.width, fb.height);
			goto release;
		}
		else if (strncmp(argv[optind], "-size", 5) == 0 && ++optind < argc) {
			w3m_getsize(&img[0], argv[optind]);
			goto release;
		}
		optind++;
	}

	/* main loop */
    while (fgets(buf, BUFSIZE, stdin) != NULL) {
		length = strlen(buf);
		buf[length - 1] = '\0'; /* chomp '\n' */

		if (DEBUG)
			fprintf(stderr, "stdin: %s\n", buf);

		reset_parm(&parm);
		parse_arg(buf, &parm, ';', isgraph);

		if (parm.argc <= 0)
			continue;

		op = str2num(parm.argv[0]);
		if (op < 0 || op >= NUM_OF_W3M_FUNC)
			continue;

		switch (op) {
			case W3M_DRAW:
			case W3M_REDRAW:
				w3m_draw(&fb, img, &parm, op);
				break;
			case W3M_STOP:
				w3m_stop();
				break;
			case W3M_SYNC:
				w3m_sync();
				break;
			case W3M_NOP:
				w3m_nop();
				break;
			case W3M_GETSIZE:
				if (parm.argc != 2) 
					break;
				w3m_getsize(&img[0], parm.argv[1]);
				break;
			case W3M_CLEAR:
				w3m_clear(img, &parm);
				break;
			default:
				break;
		}

		fflush(stdout);
		fflush(stderr);
    }

	/* release */
release:
	for (i = 0; i < MAX_IMAGE; i++)
		free_image(&img[i]);
	fb_die(&fb);

	return EXIT_SUCCESS;
}
