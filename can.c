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

struct can_data {
	int can_id;
};

static struct can_data data;

static ssize_t can_write(struct ios_ops *ios, const void *buf, size_t count)
{
	size_t loopcount;
	ssize_t ret = 0, err;

	struct can_frame to_can = {
		.can_id = data.can_id,
	};

	while (count > 0) {
		loopcount = min(count, sizeof(to_can.data));
		memcpy(to_can.data, buf, loopcount);
		to_can.can_dlc = loopcount;
retry:
		err = write(ios->fd, &to_can, sizeof(to_can));
		if (err < 0 && errno == EINTR)
			goto retry;

		if (err < 0)
			return err;

		assert(err == sizeof(to_can));
		buf += loopcount;
		count -= loopcount;
		ret += loopcount;
	}

	return ret;
}

static ssize_t can_read(struct ios_ops *ios, void *buf, size_t count)
{
	struct can_frame from_can;
	ssize_t ret;

retry:
	ret = read(ios->fd, &from_can, sizeof(from_can));
	if (ret < 0 && errno != EINTR)
		goto retry;

	if (ret < 0)
		return ret;

	assert(count >= from_can.can_dlc);
	memcpy(buf, from_can.data, from_can.can_dlc);

	return from_can.can_dlc;
}

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
	close(ios->fd);
	free(ios);
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

	ios->write = can_write;
	ios->read = can_read;
	ios->set_speed = can_set_speed;
	ios->set_flow = can_set_flow;
	ios->send_break = can_send_break;
	ios->exit = can_exit;
	ios->istelnet = false;

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

	ios->fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
	if (ios->fd < 0) {
		perror("socket");
		return NULL;
	}

	if (setsockopt(ios->fd, SOL_CAN_RAW, CAN_RAW_FILTER,
		       filter, sizeof(filter))) {
		perror("setsockopt");
		return NULL;
	}

	strcpy(ifr.ifr_name, interface);
	if (ioctl(ios->fd, SIOCGIFINDEX, &ifr)) {
		printf("%s: %s\n", interface, strerror(errno));
		return NULL;
	}
	addr.can_ifindex = ifr.ifr_ifindex;

	if (bind(ios->fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return NULL;
	}

	printf("connected to %s (rx_id=%x, tx_id=%x)\n",
	       interface, filter->can_id, data.can_id);

	return ios;
}
