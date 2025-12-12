// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 1999 Anca and Lucian Jurubita <ljurubita@hotmail.com>.
#include "config.h"

#include "microcom.h"
#include <stdbool.h>

#define BUFSIZE 1024

static int logfd = -1;
char *answerback;

static void write_receive_buf(const unsigned char *buf, int len)
{
	if (len <= 0)
		return;

	write(STDOUT_FILENO, buf, len);
	if (logfd >= 0)
		write(logfd, buf, len);
}

static int handle_receive_buf(struct ios_ops *ios, unsigned char *buf, int len)
{
	unsigned char *sendbuf = buf;

	while (len > 0) {
		switch (*buf) {
		case 5:
			write_receive_buf(sendbuf, buf - sendbuf);
			if (answerback)
				ios->write(ios, answerback, strlen(answerback));
			else
				write_receive_buf(buf, 1);

			buf += 1;
			len -= 1;
			sendbuf = buf;
			break;
		default:
			buf += 1;
			len -= 1;
			break;
		}
	}

	write_receive_buf(sendbuf, buf - sendbuf);
	return 0;
}

/* handle escape characters, writing to output */
static void cook_buf(struct ios_ops *ios, unsigned char *buf, int num)
{
	int current = 0;

	while (current < num) { /* big while loop, to process all the charactes in buffer */

		/* look for the next escape character (Ctrl-\) */
		while ((current < num) && (buf[current] != CTRL(escape_char)))
			current++;
		/* and write the sequence before esc char to the comm port */
		if (current)
			ios->write(ios, buf, current);

		if (current < num) { /* process an escape sequence */
			/* found an escape character */
			do_commandline();
			return;
		}                    /* if - end of processing escape sequence */
		num -= current;
		buf += current;
		current = 0;
	}                       /* while - end of processing all the charactes in the buffer */
}

void logfile_close(void)
{
	if (logfd >= 0)
		close(logfd);

	logfd = -1;
}

int logfile_open(const char *path)
{
	int fd;

	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (fd < 0) {
		fprintf(stderr, "Cannot open logfile '%s': %s\n", path, strerror(errno));
		return fd;
	}

	if (logfd >= 0)
		logfile_close();

	logfd = fd;

	return 0;
}

/* main program loop */
int mux_loop(struct ios_ops *ios)
{
	fd_set ready;   /* used for select */
	int i = 0, len; /* used in the multiplex loop */
	unsigned char buf[BUFSIZE];

	while (1) {
		int ret;

		FD_ZERO(&ready);
		if (!listenonly)
			FD_SET(STDIN_FILENO, &ready);
		FD_SET(ios->fd, &ready);

		select(ios->fd + 1, &ready, NULL, NULL, NULL);

		if (FD_ISSET(ios->fd, &ready)) {
			/* pf has characters for us */
			len = ios->read(ios, buf, BUFSIZE);
			if (len < 0) {
				if (errno != EAGAIN && errno != EWOULDBLOCK) {
					ret = -errno;
					fprintf(stderr, "%s\n", strerror(-ret));
					return ret;
				}
			} else if (len == 0) {
				fprintf(stderr, "Got EOF from port\n");
				return -EINVAL;
			} else {
				i = handle_receive_buf(ios, buf, len);
				if (i < 0) {
					fprintf(stderr, "%s\n", strerror(-i));
					return i;
				}
			}
		}

		if (!listenonly && FD_ISSET(STDIN_FILENO, &ready)) {
			/* standard input has characters for us */
			i = read(STDIN_FILENO, buf, BUFSIZE);
			if (i < 0) {
				ret = -errno;
				fprintf(stderr, "%s\n", strerror(-ret));
				return ret;
			}
			if (i == 0) {
				fprintf(stderr, "Got EOF from stdin\n");
				return -EINVAL;
			}

			cook_buf(ios, buf, i);
		}
	}
}
