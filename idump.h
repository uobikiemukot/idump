/* See LICENSE for licence details. */
#define _XOPEN_SOURCE 600
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <linux/fb.h>
//#include <setjmp.h>
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
	/* for parsearg.h */
	//MAX_ARGS         = 128,
};

static const char *logfile = "/tmp/idump.log";
FILE *logfp = NULL;
