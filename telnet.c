// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 2008, 2009 Sascha Hauer <s.hauer@pengutronix.de>
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

struct telnet_ios {
	struct ios_ops base;
	unsigned char iac_buf[8];
	ssize_t iac_buf_len;
};

static int telnet_printf(struct ios_ops *ios, const char *format, ...)
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
		ret = write(ios->fd, buf + written, size - written);
		if (ret < 0)
			return ret;

		written += ret;
		assert(written <= size);
	}

	return written;
}


static ssize_t telnet_write(struct ios_ops *ios, const unsigned char *buf, size_t count)
{
	size_t handled = 0;
	ssize_t ret;
	unsigned char *iac;

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


static ssize_t bufskip(unsigned char *buf, ssize_t skip, ssize_t buf_len)
{
	if (debug) {
		for (size_t i = 0; i < skip; i++) {
			printf("%02X ", buf[i]);
		}
		printf("\n");
	}
	memmove(buf, buf + skip, buf_len - skip);
	return skip;
}

static void telnet_handle_triplet(unsigned char cmd, unsigned char opt)
{
	switch (cmd) {
	case DO:
		telnet_printf(ios, "%c%c%c", IAC, WONT, opt);
		break;
	case WILL:
		telnet_printf(ios, "%c%c%c", IAC, DONT, opt);
		break;
	}
}

/**
 * process command sequence
 * @ios ios struct
 * @buf first byte of command sequence (an IAC if this isn't a continuation of an ongoing sequence)
 * @buf_len total number of bytes available at buf
 *
 * Returns:
 *  the number of bytes that must be stripped from the buffer
 *
 * Description:
 *   In order to properly handle sequences received split across buffers, priv->iac_buf stores
 *   the current partially received section of a command.
 *
 *   Commands as relevant for bytesteam processing:
 *    - escape IAC,IAC to a literal IAC/255/0xff
 *    - rfc 854 without option: IAC,[240-250] two byte seq
 *    - rfc 854 with 1 byte opt: IAC,[251-254]
 *    - rfc 854/2217 subneg: IAC,SB,...,IAC,SE
 */
static ssize_t handle_iac(struct ios_ops *ios, unsigned char *buf, ssize_t buf_len)
{
	struct telnet_ios *priv = (struct telnet_ios *)ios;
	ssize_t consumed = 0;

	assert(buf_len > 0);

	if (priv->iac_buf_len == 0) {
		if (buf[0] == IAC) {
			priv->iac_buf[0] = buf[consumed];
			priv->iac_buf_len++;
		} else {
			return 0;
		}
		consumed++;
	}

	if (consumed == buf_len)
		return consumed;

	if (priv->iac_buf_len == 1) {
		if (buf[consumed] == IAC) {
			/* sequence was IAC, IAC, consume only one to produce literal IAC in output */
			priv->iac_buf_len = 0;
			return consumed;
		} else if (buf[consumed] >= 240 && buf[consumed] <= 249) {
			/* two byte sequence: skip it */
			/* TODO: handle 2B telnet */
			priv->iac_buf_len = 0;
			return consumed + 1;
		} else if (buf[consumed] >= 251 && buf[consumed] <= 254) {
			priv->iac_buf[1] = buf[consumed];
			priv->iac_buf_len++;
		} else if (buf[consumed] == SB) {
			priv->iac_buf[1] = buf[consumed];
			priv->iac_buf_len++;
		} else {
			/* unknown/invalid sequence, interpret as unknown 2 byte seq */
			priv->iac_buf_len = 0;
			return consumed + 1;
		}
		consumed++;
	}

	if (consumed == buf_len)
		return consumed;

	if (priv->iac_buf[1] >= 251 && priv->iac_buf[1] <= 254) {
		/* three byte sequence: skip it */
		telnet_handle_triplet(priv->iac_buf[1], buf[consumed]);
		priv->iac_buf_len = 0;
		return consumed + 1;
		consumed++;
	} else if (priv->iac_buf[1] == SB) {
		/* we're within a subneg, skip until IAC,SE that isn't preceeded by another IAC */
		/* TODO: handle subneg content */
		for (; consumed < buf_len; consumed++) {
			if (buf[consumed] == IAC) {
				if (priv->iac_buf_len == 2) {
					priv->iac_buf[2] = buf[consumed];
					priv->iac_buf_len++;
				} else {
					assert(priv->iac_buf[2] == IAC);
					assert(priv->iac_buf_len == 3);
					priv->iac_buf_len--;
				}
			} else if (priv->iac_buf_len == 3 && buf[consumed] == SE) {
				priv->iac_buf_len = 0;
				return consumed;
			}
		}
		return consumed;
	}
	fprintf(stderr, "parse error\n");
	exit(1);

}

static ssize_t telnet_read(struct ios_ops *ios, unsigned char *buf, size_t count)
{
	ssize_t ret;
	unsigned char *iac;
	size_t handled = 0;

	ret = read(ios->fd, buf, count);

	if (ret <= 0)
		return ret;

	/* possibly unfinished command sequence from previous buffer */
	ret -= bufskip(buf, handle_iac(ios, buf, ret), ret);
	while (++handled < ret) {
		iac = memchr(buf + handled, IAC, ret - handled);
		if (iac) {
			handled = iac - buf;
			ret -= bufskip(iac, handle_iac(ios, iac, ret - handled), ret - handled);
		} else {
			break;
		}
	}
	return ret;
}

static int telnet_set_speed(struct ios_ops *ios, unsigned long speed)
{
	unsigned char buf2[14] = { IAC, SB, TELNET_OPTION_COM_PORT_CONTROL, SET_BAUDRATE_CS };
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
	unsigned char buf2[] = { IAC, SB, TELNET_OPTION_COM_PORT_CONTROL, SET_CONTROL_CS, 0, IAC, SE };

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
	unsigned char buf2[] = { IAC, BREAK };

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
	struct telnet_ios *telnet_ios;
	struct ios_ops *ios;
	char connected_host[256], connected_port[30];

	telnet_ios = calloc(1, sizeof(struct telnet_ios));
	if (!ios)
		return NULL;
	ios = &telnet_ios->base;

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
		dbg_printf("-> DO BINARY_TRANSMISSION\n");
		dprintf(sock, "%c%c%c", IAC, DO, TELNET_OPTION_BINARY_TRANSMISSION);
		dbg_printf("-> WILL BINARY_TRANSMISSION\n");
		dprintf(sock, "%c%c%c", IAC, WILL, TELNET_OPTION_BINARY_TRANSMISSION);
		goto out;
	}

	perror("failed to connect");
	free(ios);
	ios = NULL;
out:
	freeaddrinfo(addrinfo);
	return ios;
}

