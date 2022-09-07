/******************************************************************
** File: serial.c
** Description: the serial part for microcom project
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
** Rev. 1.01 - March 2000
** Rev. 1.02 - June 2000
****************************************************************************/

#include <limits.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <arpa/telnet.h>

#include "microcom.h"

static struct termios pots;		/* old port termios settings to restore */

static void init_comm(struct termios *pts)
{
	/* some things we want to set arbitrarily */
	pts->c_lflag &= ~ICANON;
	pts->c_lflag &= ~(ECHO | ECHOCTL | ECHONL);
	pts->c_cflag |= HUPCL | CREAD | CLOCAL;
	pts->c_iflag |= IGNBRK;
	pts->c_cc[VMIN] = 1;
	pts->c_cc[VTIME] = 0;

	/* Standard CR/LF handling: this is a dumb terminal.
	 * Do no translation:
	 *  no NL -> CR/NL mapping on output, and
	 *  no CR -> NL mapping on input.
	 */
	pts->c_oflag &= ~ONLCR;
	pts->c_iflag &= ~ICRNL;
}

static ssize_t serial_write(struct ios_ops *ios, const void *buf, size_t count)
{
	return write(ios->fd, buf, count);
}

static ssize_t serial_read(struct ios_ops *ios, void *buf, size_t count)
{
	return read(ios->fd, buf, count);
}

static int serial_set_handshake_line(struct ios_ops *ios, int pin, int enable)
{
	int flag;
	int ret;

	switch (pin) {
	case PIN_DTR:
		flag = TIOCM_DTR;
		break;
	case PIN_RTS:
		flag = TIOCM_RTS;
		break;
	}

	if (enable)
		ret = ioctl(ios->fd, TIOCMBIS, &flag);
	else
		ret = ioctl(ios->fd, TIOCMBIC, &flag);

	return ret;
}

static const struct {
	int speed;
	speed_t flag;
} bd_to_flg[] = {
	{ 50, B50 },
	{ 75, B75 },
	{ 110, B110 },
	{ 134, B134 },
	{ 150, B150 },
	{ 200, B200 },
	{ 300, B300 },
	{ 600, B600 },
	{ 1200, B1200},
	{ 1800, B1800},
	{ 2400, B2400},
	{ 4800, B4800},
	{ 9600, B9600},
	{ 19200, B19200},
	{ 38400, B38400},
	{ 57600, B57600},
	{ 115200, B115200},
	{ 230400, B230400},
#ifdef B460800
	{ 460800, B460800},
#endif
#ifdef B500000
	{ 500000, B500000},
#endif
#ifdef B576000
	{ 576000, B576000},
#endif
#ifdef B921600
	{ 921600, B921600},
#endif
#ifdef B1000000
	{ 1000000, B1000000},
#endif
#ifdef B1152000
	{ 1152000, B1152000},
#endif
#ifdef B1500000
	{ 1500000, B1500000},
#endif
#ifdef B2000000
	{ 2000000, B2000000},
#endif
#ifdef B2500000
	{ 2500000, B2500000},
#endif
#ifdef B3000000
	{ 3000000, B3000000},
#endif
#ifdef B3500000
	{ 3500000, B3500000},
#endif
#ifdef B4000000
	{ 4000000, B4000000},
#endif
};

static int baudrate_to_flag(int speed, speed_t *flag)
{
	size_t i;

	/* possible optimisation: binary search for speed */
	for (i = 0; i < ARRAY_SIZE(bd_to_flg); ++i)
		if (bd_to_flg[i].speed == speed) {
			*flag = bd_to_flg[i].flag;
			return 0;
		}

	return -1;
}

static int serial_set_speed(struct ios_ops *ios, unsigned long speed)
{
	struct termios pts;	/* termios settings on port */
	speed_t flag;
	int ret;

	tcgetattr(ios->fd, &pts);

	ret = baudrate_to_flag(speed, &flag);
	if (ret)
		return ret;

	cfsetospeed(&pts, flag);
	cfsetispeed(&pts, flag);
	tcsetattr(ios->fd, TCSANOW, &pts);

	return 0;
}

static int serial_set_flow(struct ios_ops *ios, int flow)
{
	struct termios pts;	/* termios settings on port */
	tcgetattr(ios->fd, &pts);

	switch (flow) {
	case FLOW_NONE:
		/* no flow control */
		pts.c_cflag &= ~CRTSCTS;
		pts.c_iflag &= ~(IXON | IXOFF | IXANY);
		break;
	case FLOW_HARD:
		/* hardware flow control */
		pts.c_cflag |= CRTSCTS;
		pts.c_iflag &= ~(IXON | IXOFF | IXANY);
		break;
	case FLOW_SOFT:
		/* software flow control */
		pts.c_cflag &= ~CRTSCTS;
		pts.c_iflag |= IXON | IXOFF | IXANY;
		break;
	}

	tcsetattr(ios->fd, TCSANOW, &pts);

	return 0;
}

static int serial_send_break(struct ios_ops *ios)
{
	tcsendbreak(ios->fd, 0);

	return 0;
}

/* restore original terminal settings on exit */
static void serial_exit(struct ios_ops *ios)
{
	tcsetattr(ios->fd, TCSANOW, &pots);
	close(ios->fd);
	free(ios);
}

struct ios_ops * serial_init(char *device)
{
	struct termios pts;	/* termios settings on port */
	struct ios_ops *ops;
	int fd, ret;

	ops = malloc(sizeof(*ops));
	if (!ops)
		return NULL;

	ops->write = serial_write;
	ops->read = serial_read;
	ops->set_speed = serial_set_speed;
	ops->set_flow = serial_set_flow;
	ops->set_handshake_line = serial_set_handshake_line;
	ops->send_break = serial_send_break;
	ops->exit = serial_exit;
	ops->istelnet = false;

	/* open the device */
	fd = open(device, O_RDWR | O_NONBLOCK);
	ops->fd = fd;

	if (fd < 0)
		main_usage(2, "cannot open device", device);

	/* try to lock the device */
	ret = flock(fd, LOCK_EX | LOCK_NB);
	if (ret) {
		if (!opt_force)
			main_usage(3, "could not lock port", device);
		else
			printf("could not lock port, ignoring\n");
	}

	/* modify the port configuration */
	tcgetattr(fd, &pts);
	memcpy(&pots, &pts, sizeof (pots));
	init_comm(&pts);
	tcsetattr(fd, TCSANOW, &pts);
	printf("connected to %s\n", device);

	return ops;
}

