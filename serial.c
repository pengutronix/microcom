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
#include <sys/ioctl.h>
#include <errno.h>
#include <arpa/telnet.h>

#include "microcom.h"

static struct termios pots;		/* old port termios settings to restore */
static char *lockfile;

static void init_comm(struct termios *pts)
{
	/* some things we want to set arbitrarily */
	pts->c_lflag &= ~ICANON;
	pts->c_lflag &= ~(ECHO | ECHOCTL | ECHONL);
	pts->c_cflag |= HUPCL;
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

/* unlink the lockfile */
static void serial_unlock()
{
	if (lockfile)
		unlink(lockfile);
}

/* restore original terminal settings on exit */
static void serial_exit(struct ios_ops *ios)
{
	tcsetattr(ios->fd, TCSANOW, &pots);
	close(ios->fd);
	free(ios);
	serial_unlock();
}

#define BUFLEN 512

struct ios_ops * serial_init(char *device)
{
	struct termios pts;	/* termios settings on port */
	struct ios_ops *ops;
	int fd;
	char *substring;
	long pid;
	int ret;

	ops = malloc(sizeof(*ops));
	if (!ops)
		return NULL;
	lockfile = malloc(BUFLEN);
	if (!lockfile)
		return NULL;

	ops->set_speed = serial_set_speed;
	ops->set_flow = serial_set_flow;
	ops->set_handshake_line = serial_set_handshake_line;
	ops->send_break = serial_send_break;
	ops->exit = serial_exit;

	/* check lockfile */
	substring = strrchr(device, '/');
	if (substring)
		substring++;
	else
		substring = device;

	ret = snprintf(lockfile, BUFLEN, "/var/lock/LCK..%s", substring);
	if (ret >= BUFLEN) {
		printf("path to lockfile too long\n");
		exit(1);
	}

relock:
	fd = open(lockfile, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0444);
	if (fd < 0) {
		if (errno == EEXIST) {
			char pidbuf[12];
			ssize_t nbytes = 0;
			if (opt_force) {
				printf("lockfile for port exists, ignoring\n");
				serial_unlock();
				goto relock;
			}

			fd = open(lockfile, O_RDONLY);
			if (fd < 0)
				main_usage(3, "lockfile for port can't be opened", device);

			do {
				ret = read(fd, &pidbuf[nbytes], sizeof(pidbuf) - nbytes - 1);
				nbytes += ret;
			} while (ret > 0 && nbytes < sizeof (pidbuf) - 1);

			if (ret >= 0) {
				pidbuf[nbytes] = '\0';
				ret = sscanf(pidbuf, "%10ld\n", &pid);

				if (ret == 1 && kill(pid, 0) < 0 && errno == ESRCH) {
					printf("lockfile contains stale pid, ignoring\n");
					serial_unlock();
					goto relock;
				}
			}

			main_usage(3, "lockfile for port exists", device);
		}

		if (opt_force) {
			printf("cannot create lockfile. ignoring\n");
			lockfile = NULL;
			goto force;
		}

		main_usage(3, "cannot create lockfile", device);
	}

	pid = getpid();
	dprintf(fd, "%10ld\n", (long)pid);
	close(fd);
force:
	/* open the device */
	fd = open(device, O_RDWR | O_NONBLOCK | O_CLOEXEC);
	ops->fd = fd;

	if (fd < 0) {
		serial_unlock();
		main_usage(2, "cannot open device", device);
	}

	/* modify the port configuration */
	tcgetattr(fd, &pts);
	memcpy(&pots, &pts, sizeof (pots));
	init_comm(&pts);
	tcsetattr(fd, TCSANOW, &pts);
	printf("connected to %s\n", device);

	return ops;
}

