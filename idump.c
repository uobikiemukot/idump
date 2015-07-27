/* See LICENSE for licence details. */
#include "idump.h"
#include "util.h"
#include "framebuffer.h"
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
		write(fd, buf, size);
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
	int angle = 0, opt;
	struct framebuffer_t fb;
	struct image_t img;

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
		file = make_temp_file(template);

	if (file == NULL) {
		logging(FATAL, "input file not found\n");
		usage();
		return EXIT_FAILURE;
	}

	/* init */
	if (!fb_init(&fb, true)) {
		logging(FATAL, "fb_init() failed\n");
		return EXIT_FAILURE;
	}

	init_image(&img);

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
		resize_image(&img, fb.width, fb.height, true);

	draw_image(&fb, &img, 0, 0, 0, 0, img.width, img.height, true);

	/* cleanup resource */
	free_image(&img);
	fb_die(&fb);

	return EXIT_SUCCESS;
}
