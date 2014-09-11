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

	/* open logfile */
	/*
	if ((logfp = efopen(logfile, "w")) == NULL)
		logging(ERROR, "couldn't open log file\n");
	else
		setvbuf(logfp, NULL, _IONBF, 0);
	*/

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
	//fclose(logfp);
	free_image(&img);
	fb_die(&fb);

	return EXIT_SUCCESS;
}
