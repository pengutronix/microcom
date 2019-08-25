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

#include <stdlib.h>
#include <arpa/telnet.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "microcom.h"

static int telnet_set_speed(struct ios_ops *ios, unsigned long speed)
{

	unsigned char buf2[] = {IAC, SB, TELNET_OPTION_COM_PORT_CONTROL, SET_BAUDRATE_CS, 0, 0, 0, 0, IAC, SE};
	int *speedp = (int *)&buf2[4];

	*speedp = htonl(speed);
	dbg_printf("-> IAC SB COM_PORT_CONTROL SET_BAUDRATE_CS 0x%lx IAC SE\n", speed);
	write(ios->fd, buf2, 10);

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

		/* send intent to do and accept COM_PORT stuff */
		dbg_printf("-> WILL COM_PORT_CONTROL\n");
		dprintf(sock, "%c%c%c", IAC, WILL, TELNET_OPTION_COM_PORT_CONTROL);
		dbg_printf("-> DO COM_PORT_CONTROL\n");
		dprintf(sock, "%c%c%c", IAC, DO, TELNET_OPTION_COM_PORT_CONTROL);
		goto out;
	}

	perror("failed to connect");
	free(ios);
	ios = NULL;
out:
	freeaddrinfo(addrinfo);
	return ios;
}

