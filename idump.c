/* See LICENSE for licence details. */
#include "idump.h"
#include "util.h"
#include "framebuffer.h"
#include "loader.h"
#include "image.h"

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

int main(int argc, char **argv)
{
	extern char temp_file[]; /* global */
	char *file;
	bool resize = false;
	int rotate = 0;
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
			rotate = str2num(optarg);
			break;
		default:
			break;
		}
	}

	/* init */
	fb_init(&fb);
	init_image(&img);

	/* open file */
	if (optind < argc)
		file = argv[optind];
	else
		file = make_temp_file(temp_file);

	if (load_image(file, &img) == false) {
		free_image(&img);
		return EXIT_FAILURE;
	}

	/* rotate/resize and draw */
	/* TODO: support color reduction for 8bpp mode */
	if (rotate != 0)
		rotate_image(&img, rotate);

	if (resize)
		resize_image(&img, fb.width, fb.height);

	if (img.is_anim)
		draw_anim_image(&fb, &img);
	else
		draw_image(&fb, &img);

	/* release resource */
	free_image(&img);
	fb_die(&fb);

	return EXIT_SUCCESS;
}
