/* See LICENSE for licence details. */
#include "idump.h"
#include "util.h"
#include "yafblib/yafblib.h"
#include "loader.h"
#include "image.h"

char temp_file[BUFSIZE];

void usage()
{
	printf("usage:\n"
		"\tidump [-h] [-f] [-r angle] image\n"
		"\tcat image | idump\n"
		"\twget -O - image_url | idump\n"
		"options:\n"
		"\t-h: show this help\n"
		"\t-f: fit image to display\n"
		"\t-r: rotate image (90/180/270)\n"
		"\t-c: center image\n"
		"\t-b: transparent background color (0-255)\n"
		);
}

void remove_temp_file()
{
	extern char temp_file[BUFSIZE]; /* global */
	remove(temp_file);
}

char *make_temp_file(const char *template)
{
	extern char temp_file[BUFSIZE]; /* global */
	int fd;
	ssize_t size, file_size = 0;
	char buf[BUFSIZE], *env;

	/* stdin is tty or not */
	if (isatty(STDIN_FILENO)) {
		logging(ERROR, "stdin is neither pipe nor redirect\n");
		return NULL;
	}

	/* prepare temp file */
	memset(temp_file, 0, BUFSIZE);
	if ((env = getenv("TMPDIR")) != NULL) {
		snprintf(temp_file, BUFSIZE, "%s/%s", env, template);
	} else {
		snprintf(temp_file, BUFSIZE, "/tmp/%s", template);
	}

	if ((fd = emkstemp(temp_file)) < 0)
		return NULL;
	logging(DEBUG, "tmp file:%s\n", temp_file);

	/* register cleanup function */
	if (atexit(remove_temp_file))
		logging(ERROR, "atexit() failed\nmaybe temporary file remains...\n");

	/* read data */
	while ((size = read(STDIN_FILENO, buf, BUFSIZE)) > 0) {
		if (write(fd, buf, size) != size)
			logging(ERROR, "write error\n");
		file_size += size;
	}
	eclose(fd);

	if (file_size == 0) {
		logging(ERROR, "stdin is empty\n");
		return NULL;
	}

	return temp_file;
}

int main(int argc, char **argv)
{
	const char *template = "sdump.XXXXXX";
	char *file;
	bool resize = false;
	bool center = false;
	bool blank = false;
	int angle = 0, opt;
	uint8_t alpha_background = ALPHA_BACKGROUND;
	struct framebuffer_t fb;
	struct image_t img;

	/* check arg */
	while ((opt = getopt(argc, argv, "hcfr:b:")) != -1) {
		switch (opt) {
		case 'h':
			usage();
			return EXIT_SUCCESS;
		case 'c':
			center = true;
			break;
		case 'f':
			resize = true;
			break;
		case 'r':
			angle = str2num(optarg);
			break;
		case 'b':
			alpha_background = str2num(optarg);
			break;
		default:
			break;
		}
	}

	/* open file */
	if (optind < argc)
		file = argv[optind];
	else
		file = make_temp_file(template);

	if (file == NULL) {
		logging(FATAL, "input file not found\n");
		usage();
		return EXIT_FAILURE;
	}

	/* init */
	if (!fb_init(&fb)) {
		logging(FATAL, "fb_init() failed\n");
		return EXIT_FAILURE;
	}

	if (!load_image(file, &img)) {
		logging(FATAL, "couldn't load image\n");
		fb_die(&fb);
		return EXIT_FAILURE;
	}

	/* rotate/resize and draw */
	/* TODO: support color reduction for 8bpp mode */
	if (angle != 0)
		rotate_image(&img, angle, true);

	
	if (resize)
		resize_image(&img, fb.info.width, fb.info.height, true);

	/* center image */
	if (center) {
		int posx = 0;
		int shiftx = 0;
		int posy = 0;
		int shifty = 0;
		if (fb.info.width - img.width < 0) {
			shiftx = -(fb.info.width - img.width) / 2;
		} else {
			posx = (fb.info.width - img.width) / 2;
		}
		if (fb.info.height - img.height < 0) {
			shifty = -(fb.info.height - img.height) / 2;
		} else {
			posy = (fb.info.height - img.height) / 2;
		}
		draw_image(&fb, &img, posx, posy, shiftx, shifty, img.width, img.height, alpha_background, true);
	} else {
		draw_image(&fb, &img, 0, 0, 0, 0, img.width, img.height, alpha_background, true);
	}
	/* cleanup resource */
	free_image(&img);
	fb_die(&fb);

	return EXIT_SUCCESS;
}
