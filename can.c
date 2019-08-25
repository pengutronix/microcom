/*
 * Description: the can part for microcom project
 *
 * Copyright (C) 2010 by Marc Kleine-Budde <mkl@pengutronix.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details at www.gnu.org
 *
 */

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <string.h>
#include <unistd.h>

#include <net/if.h>

#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>

#include <linux/can.h>
#include <linux/can/raw.h>

#include "microcom.h"

enum socket_local_enum {
	SOCKET_CANSIDE,
	SOCKET_TERMSIDE,
	SOCKET_MAX,
};

struct can_data {
	int socket_can;
	int socket_local[SOCKET_MAX];
	int can_id;
};

static struct can_data data;
static pthread_t can_thread;

static int can_set_speed(struct ios_ops *ios, unsigned long speed)
{
	return 0;
}

static int can_set_flow(struct ios_ops *ios, int flow)
{
	return 0;
}

static int can_send_break(struct ios_ops *ios)
{
	return 0;
}

static void can_exit(struct ios_ops *ios)
{
	shutdown(ios->fd, SHUT_RDWR);
	close(ios->fd);
	pthread_join(can_thread, NULL);
}

static void *can_thread_fun(void *_data)
{
	struct can_data *data = _data;
	struct can_frame to_can = {
		.can_id = data->can_id,
	};
	struct can_frame from_can;
	fd_set ready;
	int ret, fd_max;

	fd_max = max(data->socket_can, data->socket_local[SOCKET_CANSIDE]);
	fd_max++;

	while (1) {
		FD_ZERO(&ready);
		FD_SET(data->socket_can, &ready);
		FD_SET(data->socket_local[SOCKET_CANSIDE], &ready);

		ret = select(fd_max, &ready, NULL, NULL, NULL);
		if (ret == -1) {
			if (errno == EINTR)
				continue;
			goto exit;
		}

		/* CAN -> TERMINAL */
		if (FD_ISSET(data->socket_can, &ready)) {
			ret = read(data->socket_can, &from_can, sizeof(from_can));
			if ((ret == -1 && errno != EINTR) ||
			    ret != sizeof(from_can))
				goto exit;

			ret = write(data->socket_local[SOCKET_CANSIDE],
				    from_can.data, from_can.can_dlc);
			if ((ret == -1 && errno != EINTR) ||
			    ret != from_can.can_dlc)
				goto exit;
		}

		/* TERMINAL -> CAN */
		if (FD_ISSET(data->socket_local[SOCKET_CANSIDE], &ready)) {
			ret = read(data->socket_local[SOCKET_CANSIDE],
				   to_can.data, sizeof(to_can.data));
			if ((ret == -1 && errno != EINTR) ||
			    ret == 0)
				goto exit;

			to_can.can_dlc = ret;
			ret = write(data->socket_can, &to_can, sizeof(to_can));
			if ((ret == -1 && errno != EINTR) ||
			    ret != sizeof(to_can))
				goto exit;
		}
	}

 exit:
	shutdown(data->socket_local[SOCKET_CANSIDE], SHUT_RDWR);
	close(data->socket_local[SOCKET_CANSIDE]);
	close(data->socket_can);

	return NULL;
}

struct ios_ops *can_init(char *interface_id)
{
	struct ios_ops *ios;
	struct ifreq ifr;
	struct can_filter filter[] = {
		{
			.can_mask = CAN_SFF_MASK,
		},
	};
	struct sockaddr_can addr = {
		.can_family = PF_CAN,
	};
	char *interface = interface_id;
	char *id_str = NULL;

	ios = malloc(sizeof(*ios));
	if (!ios)
		return NULL;

	ios->set_speed = can_set_speed;
	ios->set_flow = can_set_flow;
	ios->send_break = can_send_break;
	ios->exit = can_exit;

	/*
	 * the string is supposed to be formated this way:
	 * interface:rx:tx
	 */
	if (interface_id)
		id_str = strchr(interface, ':');

	if (id_str) {
		*id_str = 0x0;
		id_str++;
		filter->can_id = strtol(id_str, NULL, 16) & CAN_SFF_MASK;

		id_str = strchr(id_str, ':');
	} else {
		filter->can_id = DEFAULT_CAN_ID;
	}

	if (id_str) {
		*id_str = 0x0;
		id_str++;
		data.can_id = strtol(id_str, NULL, 16) & CAN_SFF_MASK;
	} else {
		data.can_id = filter->can_id;
	}

	if (!interface || *interface == 0x0)
		interface = DEFAULT_CAN_INTERFACE;

	/* no cleanups on failure, we exit anyway */

	data.socket_can = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (data.socket_can < 0) {
		perror("socket");
		return NULL;
	}

	if (setsockopt(data.socket_can, SOL_CAN_RAW, CAN_RAW_FILTER,
		       filter, sizeof(filter))) {
		perror("setsockopt");
		return NULL;
	}

	strcpy(ifr.ifr_name, interface);
	if (ioctl(data.socket_can, SIOCGIFINDEX, &ifr)) {
		printf("%s: %s\n", interface, strerror(errno));
		return NULL;
	}
	addr.can_ifindex = ifr.ifr_ifindex;

	if (bind(data.socket_can, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return NULL;
	}

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, data.socket_local) < 0) {
		perror("socketpair");
		return NULL;
	}

	if (pthread_create(&can_thread, NULL, can_thread_fun, &data) != 0)
		return NULL;

	ios->fd = data.socket_local[SOCKET_TERMSIDE];
	printf("connected to %s (rx_id=%x, tx_id=%x)\n",
	       interface, filter->can_id, data.can_id);

	return ios;
}
