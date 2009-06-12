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

#include "microcom.h"

static int telnet_set_speed(struct ios_ops *ios, speed_t speed)
{

//	unsigned char buf1[] = {IAC, WILL , COM_PORT_OPTION};
	unsigned char buf2[] = {IAC, SB, COM_PORT_OPTION, SET_BAUDRATE_CS, 0, 0, 0, 0, IAC, SE};
	int *speedp = (int *)&buf2[4];

//	write(fd, buf1, 3);
	*speedp = htonl(flag_to_baudrate(speed));
	write(ios->fd, buf2, 10);

	return 0;
}

static int telnet_set_flow(struct ios_ops *ios, int flow)
{
	unsigned char buf2[] = {IAC, SB, COM_PORT_OPTION, SET_CONTROL_CS, 0, IAC, SE};

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

	write(ios->fd, buf2, sizeof(buf2));

	return 0;
}

void telnet_exit(struct ios_ops *ios)
{
	close(ios->fd);
	free(ios);
}

struct ios_ops *telnet_init(char *hostport)
{
	int sock;
	struct sockaddr_in server_in;
	char *host = hostport;
	char *portstr;
	int port = 23;
	struct hostent *hp;
	struct ios_ops *ios;

	ios = malloc(sizeof(*ios));
	if (!ios)
		return NULL;

	ios->set_speed = telnet_set_speed;
	ios->set_flow = telnet_set_flow;
	ios->exit = telnet_exit;

	portstr = strchr(hostport, ':');
	if (portstr) {
		*portstr = 0;
		portstr++;
		port = atoi(portstr);
	}

	hp = gethostbyname(host);
	if (!hp) {
		perror("gethostbyname");
		return NULL;
	}

	host = inet_ntoa(*(struct in_addr*)(hp->h_addr_list[0]));
	
	memset(&server_in, 0, sizeof(server_in));     /* Zero out structure */
	server_in.sin_family      = AF_INET;             /* Internet address family */
	server_in.sin_addr.s_addr = inet_addr(host);   /* Server IP address */
	server_in.sin_port        = htons(port); /* Server port */

	sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		printf("socket() failed\n");
		return NULL;
	}

	/* Establish the connection to the echo server */
	if (connect(sock, (struct sockaddr *) &server_in, sizeof(server_in)) < 0) {
		perror("connect");
		return NULL;
	}
	ios->fd = sock;
	printf("connected to %s (port %d)\n", host, port);
	
	return ios;
}

