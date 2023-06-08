/******************************************************************
** File: telnet.c
** Description: the telnet part for microcom project
**
** Copyright (C) 2008, 2009 Sascha Hauer <s.hauer@pengutronix.de>.
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
****************************************************************************/
#include "config.h"

#include <stdlib.h>
#include <arpa/telnet.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <string.h>

#include "microcom.h"

static int ios_printf(struct ios_ops *ios, const char *format, ...)
{
	char buf[20];
	int size, written = 0;
	ssize_t ret;
	va_list args;

	va_start(args, format);

	size = vsnprintf(buf, sizeof(buf), format, args);

	va_end(args);

	if (size >= sizeof(buf)) {
		/* truncated output */
		errno = EIO;
		return -1;
	}

	while (written < size) {
		ret = ios->write(ios, buf + written, size - written);
		if (ret < 0)
			return ret;

		written += ret;
		assert(written <= size);
	}

	return written;
}

static ssize_t get(unsigned char *buf, unsigned char *out, size_t len)
{
	if (!len)
		return -1;

	if (buf[0] == IAC) {
		if (len < 1)
			return -1;
		if (buf[1] == IAC) {
			*out = IAC;
			return 2;
		}
		return -1;
	} else {
		*out = buf[0];
		return 1;
	}
}

static size_t getl(unsigned char *buf, uint32_t *out, size_t len)
{
	*out = 0;
	int i;
	size_t offset = 0;

	for (i = 0; i < 4; ++i) {
		ssize_t getres;
		unsigned char c;

		getres = get(buf + offset, &c, len - offset);
		if (getres < 0)
			return getres;

		*out <<= 8;
		*out |= c;

		offset += getres;
	}

	return offset;
}

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
		{
			uint32_t baudrate;
			ssize_t getres = getl(buf + 2, &baudrate, len - 2);

			if (getres < 0) {
				fprintf(stderr, "Incomplete or broken SB (SET_BAUDRATE_SC)\n");
				return getres;
			}
			dbg_printf("SET_BAUDRATE_SC %u ", baudrate);
			i += getres;;
		}
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
		{
			unsigned char ctrl;
			ssize_t getres = get(buf + 2, &ctrl, len - 2);

			if (getres < 0) {
				fprintf(stderr, "Incomplete or broken SB (SET_CONTROL_SC)\n");
				return getres;
			}

			dbg_printf("SET_CONTROL_SC 0x%02x ", ctrl);
			i += getres;
		}
		break;
	case NOTIFY_LINESTATE_SC:
		dbg_printf("NOTIFY_LINESTATE_SC ");
		break;
	case NOTIFY_MODEMSTATE_SC:
		{
			unsigned char ms;
			ssize_t getres = get(buf + 2, &ms, len - 2);

			if (getres < 0) {
				fprintf(stderr, "Incomplete or broken SB (NOTIFY_MODEMSTATE_SC)\n");
				return getres;
			}

			dbg_printf("NOTIFY_MODEMSTATE_SC 0x%02x ", ms);
			i += getres;
		}
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
		dbg_printf("??? %d ", buf[1]);
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

/* This function is called with buf[0] being IAC. */
static int handle_command(struct ios_ops *ios, unsigned char *buf, int len)
{
	int ret;
	const struct telnet_option *option;

	/* possible out-of-bounds access */
	switch (buf[1]) {
	case SB:
		dbg_printf("SB ");
		ret = do_subneg(ios, &buf[2], len - 2);
		if (ret < 0)
			return ret;
		return ret + 2;

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
			ios_printf(ios, "%c%c%c", IAC, DONT, buf[2]);
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
			ios_printf(ios, "%c%c%c", IAC, WONT, buf[2]);
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

static ssize_t telnet_write(struct ios_ops *ios, const void *buf, size_t count)
{
	size_t handled = 0;
	ssize_t ret;
	void *iac;

	/*
	 * To send an IAC character in the data stream, two IACs must be sent.
	 * So find the first IAC in the data to be send (if any), send the data
	 * before that IAC unquoted, then send the double IAC. Repeat until
	 * all IACs are handled.
	 */
	while ((iac = memchr(buf + handled, IAC, count - handled)) != NULL) {
		if (iac - (buf + handled)) {
			ret = write(ios->fd, buf + handled, iac - (buf + handled));
			if (ret < 0)
				return ret;
			handled += ret;
		} else {
			dprintf(ios->fd, "%c%c", IAC, IAC);
			handled += 1;
		}
	}

	/* Send the remaining data that needs no quoting. */
	ret = write(ios->fd, buf + handled, count - handled);
	if (ret < 0)
		return ret;
	return ret + handled;
}

static ssize_t telnet_read(struct ios_ops *ios, void *buf, size_t count)
{
	ssize_t ret = read(ios->fd, buf, count);
	void *iac;
	size_t handled = 0;

	if (ret <= 0)
		return ret;

	while ((iac = memchr(buf + handled, IAC, ret - handled)) != NULL) {
		handled = iac - buf;

		/* XXX: possible out-of-bounds access */
		if (((unsigned char *)iac)[1] == IAC) {
			/* duplicated IAC = one payload IAC */
			ret -= 1;
			memmove(iac, iac + 1, ret - (iac - buf));
			handled += 1;
		} else {
			int iaclen = handle_command(ios, iac, ret - handled);

			if (iaclen < 0)
				return iaclen;

			memmove(iac, iac + iaclen, ret - (handled + iaclen));
			ret -= iaclen;
		}
	}
	if (ret) {
		return ret;
	} else {
		errno = EAGAIN;
		return -1;
	}
}

static int telnet_set_speed(struct ios_ops *ios, unsigned long speed)
{
	unsigned char buf2[14] = {IAC, SB, TELNET_OPTION_COM_PORT_CONTROL, SET_BAUDRATE_CS};
	size_t offset = 4;
	int i;

	for (i = 0; i < 4; ++i) {
		buf2[offset] = (speed >> (24 - 8 * i)) & 0xff;
		if (buf2[offset++] == IAC)
			buf2[offset++] = IAC;
	}

	buf2[offset++] = IAC;
	buf2[offset++] = SE;

	dbg_printf("-> IAC SB COM_PORT_CONTROL SET_BAUDRATE_CS 0x%lx IAC SE\n", speed);
	write(ios->fd, buf2, offset);

	return 0;
}

static int telnet_set_flow(struct ios_ops *ios, int flow)
{
	unsigned char buf2[] = {IAC, SB, TELNET_OPTION_COM_PORT_CONTROL, SET_CONTROL_CS, 0, IAC, SE};

	switch (flow) {
	case FLOW_NONE:
		/* no flow control */
		buf2[4] = 1;
		break;
	case FLOW_SOFT:
		/* software flow control */
		buf2[4] = 2;
		break;
	case FLOW_HARD:
		/* hardware flow control */
		buf2[4] = 3;
		break;
	}

	dbg_printf("-> IAC SB COM_PORT_CONTROL SET_CONTROL_CS %d IAC SE\n", buf2[4]);
	write(ios->fd, buf2, sizeof(buf2));

	return 0;
}

static int telnet_send_break(struct ios_ops *ios)
{
	unsigned char buf2[] = {IAC, BREAK};

	write(ios->fd, buf2, sizeof(buf2));

	return 0;
}

static void telnet_exit(struct ios_ops *ios)
{
	close(ios->fd);
	free(ios);
}

struct ios_ops *telnet_init(char *hostport)
{
	char *port;
	int ret;
	struct addrinfo *addrinfo, *ai;
	struct addrinfo hints;
	struct ios_ops *ios;
	char connected_host[256], connected_port[30];

	ios = malloc(sizeof(*ios));
	if (!ios)
		return NULL;

	ios->write = telnet_write;
	ios->read = telnet_read;
	ios->set_speed = telnet_set_speed;
	ios->set_flow = telnet_set_flow;
	ios->send_break = telnet_send_break;
	ios->exit = telnet_exit;

	memset(&hints, '\0', sizeof(hints));
	hints.ai_flags = AI_ADDRCONFIG;
	hints.ai_socktype = SOCK_STREAM;

	if (hostport[0] == '[') {
		char *s = strchr(++hostport, ']');

		if (s)
			/* terminate hostport after host portion */
			*s = '\0';

		if (s && s[1] == ':')
			port = s + 2;
		else if (s && s[1] == '\0')
			port = "23";
		else {
			fprintf(stderr, "failed to parse host:port");
			free(ios);
			return NULL;
		}
	} else {
		port = strchr(hostport, ':');
		if (port) {
			/* terminate hostport after host portion */
			*port = '\0';

			port += 1;
		} else
			port = "23";
	}

	ret = getaddrinfo(hostport, port, &hints, &addrinfo);
	if (ret) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
		return NULL;
	}

	for (ai = addrinfo; ai != NULL; ai = ai->ai_next) {
		int sock = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (sock < 0)
			continue;

		if (connect(sock, ai->ai_addr, ai->ai_addrlen) < 0) {
			close(sock);
			continue;
		}

		ios->fd = sock;

		ret = getnameinfo(ai->ai_addr, ai->ai_addrlen,
				  connected_host, sizeof(connected_host),
				  connected_port, sizeof(connected_port),
				  NI_NUMERICHOST | NI_NUMERICSERV);
		if (ret) {
			fprintf(stderr, "getnameinfo: %s\n", gai_strerror(ret));
			goto out;
		}
		printf("connected to %s (port %s)\n", connected_host, connected_port);

		/* send intent we WILL do COM_PORT stuff */
		dbg_printf("-> WILL COM_PORT_CONTROL\n");
		dprintf(sock, "%c%c%c", IAC, WILL, TELNET_OPTION_COM_PORT_CONTROL);
		goto out;
	}

	perror("failed to connect");
	free(ios);
	ios = NULL;
out:
	freeaddrinfo(addrinfo);
	return ios;
}

