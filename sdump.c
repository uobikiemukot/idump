/* See LICENSE for licence details. */
#include "idump.h"
#include "util.h"
#include "framebuffer.h"
#include "loader.h"
#include "image.h"

#include <sixel.h>

char temp_file[] = "/tmp/idump.XXXXXX";

enum {
	SIXEL_COLORS = 256,
	SIXEL_BPP    = 3,
};

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

int sixel_write_callback(char *data, int size, void *priv)
{
	FILE *fp = (FILE *) priv;
	/*
	for (int i = 0; i < size; i++) {
		if (i == (size - 1))
			break;

		if (*(data + i) == 0x1B && *(data + i + 1) == 0x5C) {
			fprintf(fp, "\033\033\\\033P\\");
			break;
		} else {
			fwrite(data + i, 1, 1, fp);
		}
	}

	logging(DEBUG, "write callback() size:%d\n", size);
	return size;
	*/
	return fwrite(data, size, 1, fp);
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
	sixel_output_t *sixel_context = NULL;
	sixel_dither_t *sixel_dither = NULL;

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

	if (file == NULL) {
		logging(FATAL, "input file not found\n");
		return EXIT_FAILURE;
	}

	/* init */
	fb_init(&fb);
	init_image(&img);

	if (load_image(file, &img) == false)
		goto cleanup;

	/* rotate/resize and draw */
	/* TODO: support color reduction for 8bpp mode */
	if (rotate != 0)
		rotate_image(&img, rotate);

	if (resize)
		resize_image(&img, fb.width, fb.height);

	/* sixel */
	/* FIXME: libsixel only allows 3 bytes per pixel image,
		we should convert bpp when bpp is 1 or 2 or 4... */
	if ((sixel_dither = sixel_dither_create(SIXEL_COLORS)) == NULL) {
		logging(ERROR, "couldn't create dither\n");
		goto cleanup;
	}

	if (sixel_dither_initialize(sixel_dither, img.anim ? img.anim[0]: img.data, img.width, img.height,
		//SIXEL_BPP /* must be "3" */, LARGE_AUTO, REP_AUTO, QUALITY_AUTO) != 0) {
		img.channel, LARGE_AUTO, REP_AUTO, QUALITY_AUTO) != 0) {
		logging(ERROR, "couldn't initialize dither\n");
		sixel_dither_unref(sixel_dither);
		goto cleanup;
	}

	sixel_dither_set_diffusion_type(sixel_dither, DIFFUSE_AUTO);

	if ((sixel_context = sixel_output_create(sixel_write_callback, stdout)) == NULL) {
		logging(ERROR, "couldn't create sixel context\n");
		goto cleanup;
	}

	sixel_output_set_8bit_availability(sixel_context, CSIZE_7BIT);

	//printf("\033P");
	//sixel_encode(img.anim ? img.anim[0]: img.data, img.width, img.height, SIXEL_BPP, sixel_dither, sixel_context);
	sixel_encode(img.anim ? img.anim[0]: img.data, img.width, img.height, img.channel, sixel_dither, sixel_context);
	//printf("\033\\");

	//draw_image(&fb, &img, 0, 0, 0, 0, img.width, img.height, true);

	/* cleanup resource */
cleanup:
	if (sixel_dither)
		sixel_dither_unref(sixel_dither);
	if (sixel_context)
		sixel_output_unref(sixel_context);
	free_image(&img);
	fb_die(&fb);
	//fclose(logfp);

	return EXIT_SUCCESS;
}
