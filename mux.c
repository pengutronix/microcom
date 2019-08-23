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
#include <stdbool.h>

#define BUFSIZE 1024

/* This is called with buf[-2:0] being IAC SB COM_PORT_OPTION */
static int do_com_port_option(struct ios_ops *ios, unsigned char *buf, int len)
{
	int i = 2;

	switch (buf[1]) {
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
		dbg_printf("SET_BAUDRATE_SC %d ",
			buf[2] << 24 | buf[3] << 16 | buf[4] << 8 | buf[5]);
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
		dbg_printf("??? %d ", buf[i]);
		break;
	}

	while (i < len) {
		if (buf[i] == IAC) {
			if (i + 1 < len && buf[i+1] == IAC) {
				/* quoted IAC -> unquote */
				++i;
			} else if (i + 1 < len && buf[i+1] == SE) {
				dbg_printf("IAC SE\n");
				return i + 2;
			}
		}
		dbg_printf("%d ", buf[i]);

		++i;
	}

	fprintf(stderr, "Incomplete SB string\n");
	return -EINVAL;
}

struct telnet_option {
	unsigned char id;
	const char *name;
	int (*subneg_handler)(struct ios_ops *ios, unsigned char *buf, int len);
	bool sent_will;
};

#define TELNET_OPTION(x)	.id = TELNET_OPTION_ ## x, .name = #x

static const struct telnet_option telnet_options[] = {
	{
		TELNET_OPTION(COM_PORT_CONTROL),
		.subneg_handler = do_com_port_option,
		.sent_will = true,
	}, {
		TELNET_OPTION(BINARY_TRANSMISSION),
	}, {
		TELNET_OPTION(ECHO),
	}, {
		TELNET_OPTION(SUPPRESS_GO_AHEAD),
	}
};

static const struct telnet_option *get_telnet_option(unsigned char id)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(telnet_options); ++i) {
		if (id == telnet_options[i].id)
			return &telnet_options[i];
	}

	return NULL;
}


/* This function is called with buf[-2:-1] being IAC SB */
static int do_subneg(struct ios_ops *ios, unsigned char *buf, int len)
{
	const struct telnet_option *option = get_telnet_option(buf[0]);

	if (option)
		dbg_printf("%s ", option->name);
	if (option->subneg_handler) {
		return option->subneg_handler(ios, buf, len);
	} else {
		/* skip over subneg string */
		int i;
		for (i = 0; i < len - 1; ++i) {
			if (buf[i] != IAC) {
				dbg_printf("%d ", buf[i]);
				continue;
			}

			if (buf[i + 1] == SE) {
				dbg_printf("IAC SE\n");
				return i + 1;
			}

			/* skip over IAC IAC */
			if (buf[i + 1] == IAC) {
				dbg_printf("%d \n", IAC);
				i++;
			}
		}

		/* the subneg string isn't finished yet */
		if (i == len - 1)
			dbg_printf("%d", buf[i]);
		dbg_printf("\\\n");
		fprintf(stderr, "Incomplete SB string\n");

		return -EINVAL;
	}
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

/* This function is called with buf[0] being IAC. */
static int handle_command(struct ios_ops *ios, unsigned char *buf, int len)
{
	int ret;
	const struct telnet_option *option;

	switch (buf[1]) {
	case SB:
		dbg_printf("SB ");
		ret = do_subneg(ios, &buf[2], len - 2);
		if (ret < 0)
			return ret;
		return ret + 2;

	case IAC:
		/* escaped IAC -> unescape */
		write_receive_buf(&buf[1], 1);
		return 2;

	case WILL:
		option = get_telnet_option(buf[2]);
		if (option)
			dbg_printf("WILL %s", option->name);
		else
			dbg_printf("WILL #%d", buf[2]);

		if (option && option->subneg_handler) {
			/* ok, we already requested that, so take this as
			 * confirmation to actually do COM_PORT stuff.
			 * Everything is fine. Don't reconfirm to prevent an
			 * request/confirm storm.
			 */
			dbg_printf("\n");
		} else {
			/* unknown/unimplemented option -> DONT */
			dbg_printf(" -> DONT\n");
			dprintf(ios->fd, "%c%c%c", IAC, DONT, buf[2]);
		}
		return 3;

	case WONT:
		option = get_telnet_option(buf[2]);
		if (option)
			dbg_printf("WONT %s\n", option->name);
		else
			dbg_printf("WONT #%d\n", buf[2]);
		return 3;

	case DO:
		option = get_telnet_option(buf[2]);
		if (option)
			dbg_printf("DO %s", option->name);
		else
			dbg_printf("DO #%d", buf[2]);

		if (option && option->sent_will) {
			/*
			 * This is a confirmation of an WILL sent by us before.
			 * There is nothing to do now.
			 */
			dbg_printf("\n");
		} else {
			/* Oh, cannot handle that one, so send a WONT */
			dbg_printf(" -> WONT\n");
			dprintf(ios->fd, "%c%c%c", IAC, WONT, buf[2]);
		}
		return 3;

	case DONT:
		option = get_telnet_option(buf[2]);
		if (option)
			dbg_printf("DONT %s\n", option->name);
		else
			dbg_printf("DONT #%d\n", buf[2]);
		return 3;

	default:
		dbg_printf("??? %d\n", buf[1]);
		return 1;
	}
}

static int handle_receive_buf(struct ios_ops *ios, unsigned char *buf, int len)
{
	unsigned char *sendbuf = buf;
	int i;

	while (len) {
		switch (*buf) {
		case IAC:
			/* BUG: this is telnet specific */
			write_receive_buf(sendbuf, buf - sendbuf);
			i = handle_command(ios, buf, len);
			if (i < 0)
				return i;

			buf += i;
			len -= i;
			sendbuf = buf;
			break;
		case 5:
			write_receive_buf(sendbuf, buf - sendbuf);
			if (answerback)
				write(ios->fd, answerback, strlen(answerback));
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

	fd = open(path, O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0644);
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
		int ret;

		FD_ZERO(&ready);
		if (!listenonly)
			FD_SET(STDIN_FILENO, &ready);
		FD_SET(ios->fd, &ready);

		select(ios->fd + 1, &ready, NULL, NULL, NULL);

		if (FD_ISSET(ios->fd, &ready)) {
			/* pf has characters for us */
			len = read(ios->fd, buf, BUFSIZE);
			if (len < 0) {
				ret = -errno;
				fprintf(stderr, "%s\n", strerror(-ret));
				return ret;
			}
			if (len == 0) {
				fprintf(stderr, "Got EOF from port\n");
				return -EINVAL;
			}

			i = handle_receive_buf(ios, buf, len);
			if (i < 0) {
				fprintf(stderr, "%s\n", strerror(-i));
				return i;
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
