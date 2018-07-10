/***************************************************************************
** File: mux.c
** Description: the main program loop
**
** Copyright (C)1999 Anca and Lucian Jurubita <ljurubita@hotmail.com>.
** All rights reserved.
****************************************************************************
** This program is free software; you can redistribute it and/or
** modify it under the terms of the GNU General Public License
** as published by the Free Software Foundation; either version 2
** of the License, or (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details at www.gnu.org
****************************************************************************
** Rev. 1.0 - Feb. 2000
****************************************************************************/
#include "microcom.h"
#include <arpa/telnet.h>
#include <arpa/inet.h>

#define BUFSIZE 1024

static int do_com_port_option(unsigned char *buf, int len)
{
	int i = 0;

	while (i < len) {
		switch (buf[i]) {
		case IAC:
			dbg_printf("IAC ");
			return i + 1;
		case SET_BAUDRATE_CS:
			dbg_printf("SET_BAUDRATE_CS ");
			break;
		case SET_DATASIZE_CS:
			dbg_printf("SET_DATASIZE_CS ");
			break;
		case SET_PARITY_CS:
			dbg_printf("SET_PARITY_CS ");
			break;
		case SET_STOPSIZE_CS:
			dbg_printf("SET_STOPSIZE_CS ");
			break;
		case SET_CONTROL_CS:
			dbg_printf("SET_CONTROL_CS ");
			break;
		case NOTIFY_LINESTATE_CS:
			dbg_printf("NOTIFY_LINESTATE_CS ");
			break;
		case NOTIFY_MODEMSTATE_CS:
			dbg_printf("NOTIFY_MODEMSTATE_CS ");
			break;
		case FLOWCONTROL_SUSPEND_CS:
			dbg_printf("FLOWCONTROL_SUSPEND_CS ");
			break;
		case FLOWCONTROL_RESUME_CS:
			dbg_printf("FLOWCONTROL_RESUME_CS ");
			break;
		case SET_LINESTATE_MASK_CS:
			dbg_printf("SET_LINESTATE_MASK_CS ");
			break;
		case SET_MODEMSTATE_MASK_CS:
			dbg_printf("SET_MODEMSTATE_MASK_CS ");
			break;
		case PURGE_DATA_CS:
			dbg_printf("PURGE_DATA_CS ");
			break;
		case SET_BAUDRATE_SC:
			dbg_printf("SET_BAUDRATE_SC %d ", ntohl(*(int *)&buf[i + 1]));
			i += 4;
			break;
		case SET_DATASIZE_SC:
			dbg_printf("SET_DATASIZE_SC ");
			break;
		case SET_PARITY_SC:
			dbg_printf("SET_PARITY_SC ");
			break;
		case SET_STOPSIZE_SC:
			dbg_printf("SET_STOPSIZE_SC ");
			break;
		case SET_CONTROL_SC:
			i++;
			dbg_printf("SET_CONTROL_SC 0x%02x ", buf[i]);
			break;
		case NOTIFY_LINESTATE_SC:
			dbg_printf("NOTIFY_LINESTATE_SC ");
			break;
		case NOTIFY_MODEMSTATE_SC:
			i++;
			dbg_printf("NOTIFY_MODEMSTATE_SC 0x%02x ", buf[i]);
			break;
		case FLOWCONTROL_SUSPEND_SC:
			dbg_printf("FLOWCONTROL_SUSPEND_SC ");
			break;
		case FLOWCONTROL_RESUME_SC:
			dbg_printf("FLOWCONTROL_RESUME_SC ");
			break;
		case SET_LINESTATE_MASK_SC:
			dbg_printf("SET_LINESTATE_MASK_SC ");
			break;
		case SET_MODEMSTATE_MASK_SC:
			dbg_printf("SET_MODEMSTATE_MASK_SC ");
			break;
		case PURGE_DATA_SC:
			dbg_printf("PURGE_DATA_SC ");
			break;
		default:
			dbg_printf("%d ", buf[i]);
			break;
		}
		i++;
	}

	return len;
}

static int do_subneg(unsigned char *buf, int len)
{
	int i = 0;

	while (i < len) {
		switch (buf[i]) {
		case COM_PORT_OPTION:
			dbg_printf("COM_PORT_OPTION ");
			return do_com_port_option(&buf[i + 1], len - i) + 1;
		case IAC:
			dbg_printf("IAC ");
			return len - i;
		default:
			dbg_printf("%d ", buf[i]);
			break;
		}
		i++;
	}

	return len;
}

static int handle_command(unsigned char *buf, int len)
{
	int i = 0;

	while (i < len) {
		switch (buf[i]) {
		case SB:
			dbg_printf("SB ");
			i += do_subneg(&buf[i+1], len - i);
			break;
		case IAC:
			dbg_printf("IAC ");
			break;
		case COM_PORT_OPTION:
			dbg_printf("COM_PORT_OPTION ");
			break;
		case SE:
			dbg_printf("SE ");
			break;
		case WILL:
			dbg_printf("WILL ");
			break;
		case WONT:
			dbg_printf("WONT ");
			break;
		case DO:
			dbg_printf("DO ");
			break;
		case DONT:
			dbg_printf("DONT ");
			break;
		default:
			dbg_printf("%d ", buf[i]);
			break;
		}
		i++;
	}

	dbg_printf("\n");
	return len;
}

static int logfd = -1;
char *answerback;

static void write_receive_buf(const unsigned char *buf, int len)
{
	if (!len)
		return;

	write(STDOUT_FILENO, buf, len);
	if (logfd >= 0)
		write(logfd, buf, len);
}

static void handle_receive_buf(struct ios_ops *ios, unsigned char *buf, int len)
{
	unsigned char *sendbuf = buf;
	int i;

	while (len) {
		switch (*buf) {
		case IAC:
			/* BUG: this is telnet specific */
			write_receive_buf(sendbuf, buf - sendbuf);
			i = handle_command(buf, len);
			buf += i;
			len -= i;
			sendbuf = buf;
			break;
		case 5:
			write_receive_buf(sendbuf, buf - sendbuf);
			if (answerback)
				write(ios->fd, answerback, strlen(answerback));

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
}

/* handle escape characters, writing to output */
static void cook_buf(struct ios_ops *ios, unsigned char *buf, int num)
{
	int current = 0;

	while (current < num) {	/* big while loop, to process all the charactes in buffer */

		/* look for the next escape character (Ctrl-\) */
		while ((current < num) && (buf[current] != 28))
			current++;
		/* and write the sequence before esc char to the comm port */
		if (current)
			write(ios->fd, buf, current);

		if (current < num) {	/* process an escape sequence */
			/* found an escape character */
			do_commandline();
			return;
		}		/* if - end of processing escape sequence */
		num -= current;
		buf += current;
		current = 0;
	}			/* while - end of processing all the charactes in the buffer */
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
	fd_set ready;		/* used for select */
	int i = 0, len;		/* used in the multiplex loop */
	unsigned char buf[BUFSIZE];

	while (1) {
		FD_ZERO(&ready);
		if (!listenonly)
			FD_SET(STDIN_FILENO, &ready);
		FD_SET(ios->fd, &ready);

		select(ios->fd + 1, &ready, NULL, NULL, NULL);

		if (FD_ISSET(ios->fd, &ready)) {
			/* pf has characters for us */
			len = read(ios->fd, buf, BUFSIZE);
			if (len < 0)
				return -errno;
			if (len == 0) {
				fprintf(stderr, "Got EOF from port\n");
				return 0;
			}

			handle_receive_buf(ios, buf, len);
		}

		if (!listenonly && FD_ISSET(STDIN_FILENO, &ready)) {
			/* standard input has characters for us */
			i = read(STDIN_FILENO, buf, BUFSIZE);
			if (i < 0)
				return -errno;
			if (i == 0)
				return -EINVAL;

			cook_buf(ios, buf, i);
		}
	}
}
