/******************************************************************
** File: microcom.c
** Description: the main file for microcom project
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
#include "microcom.h"

#include <unistd.h>
#include <getopt.h>

int crnl_mapping;		//0 - no mapping, 1 mapping
char device[MAX_DEVICE_NAME];	/* serial device name */
int dolog = 0;			/* log active flag */
FILE *flog;			/* log file */
int pf = 0;			/* port file descriptor */
struct termios pots;		/* old port termios settings to restore */
struct termios sots;		/* old stdout/in termios settings to restore */

void
init_comm(struct termios *pts, int speed)
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
	crnl_mapping = 0;
	pts->c_iflag &= ~ICRNL;

	/* set hardware flow control by default */
	pts->c_cflag &= ~CRTSCTS;
	pts->c_iflag &= ~(IXON | IXOFF | IXANY);

	cfsetospeed(pts, speed);
	cfsetispeed(pts, speed);
}

void
init_stdin(struct termios *sts)
{
	/* again, some arbitrary things */
	sts->c_iflag &= ~BRKINT;
	sts->c_iflag |= IGNBRK;
	sts->c_lflag &= ~ISIG;
	sts->c_cc[VMIN] = 1;
	sts->c_cc[VTIME] = 0;
	sts->c_lflag &= ~ICANON;
	/* no local echo: allow the other end to do the echoing */
	sts->c_lflag &= ~(ECHO | ECHOCTL | ECHONL);
}

/********************************************************************
 Main functions
 ********************************************************************
 static void help_usage(int exitcode, char *error, char *addl)
      help with running the program
      - exitcode - to be returned when the program is ended
      - error - error string to be printed
      - addl - another error string to be printed
 static void cleanup_termios(int signal)
      signal handler to restore terminal set befor exit
 int main(int argc, char *argv[]) -
      main program function
********************************************************************/
void
main_usage(int exitcode, char *str, char *dev)
{
	fprintf(stderr, "Usage: microcom [options]\n"
		" [options] include:\n"
		"    -pdevfile       use the specified serial port device;\n"
		"                    if a port is not provided, microcom\n"
		"                        will try to autodetect a modem\n"
		"           example: -p/dev/ttyS3\n"
		"microcom provides session logging in microcom.log file\n");
	fprintf(stderr, "Exitcode %d - %s %s\n\n", exitcode, str, dev);
	exit(exitcode);
}

/* restore original terminal settings on exit */
void
cleanup_termios(int signal)
{
	/* cloase the log file first */
	if (dolog) {
		fflush(flog);
		fclose(flog);
	}
	tcsetattr(pf, TCSANOW, &pots);
	tcsetattr(STDIN_FILENO, TCSANOW, &sots);
	exit(0);
}

int
main(int argc, char *argv[])
{
	struct termios pts;	/* termios settings on port */
	struct termios sts;	/* termios settings on stdout/in */
	struct sigaction sact;	/* used to initialize the signal handler */
	int opt,i, speed;
	int bspeed = B115200;

	device[0] = '\0';

	struct option long_options[] = {
		{ "help", no_argument, 0, 'h' },
		{ "port", required_argument, 0, 'p'},
		{ "speed", required_argument, 0, 's'},
		{ 0, 0, 0, 0},
	};

	while ((opt = getopt_long(argc, argv, "hp:s:", long_options, NULL)) != -1) {
		switch (opt) {
			case 'h':
				main_usage(1, "", "");
				exit(0);
			case 'p':
				strncpy(device, optarg, MAX_DEVICE_NAME);
				break;
			case 's':
				speed = strtoul(optarg, NULL, 0);
				printf("speed: %d\n",speed);
				switch(speed) {
					case 50: bspeed = B50; break;
					case 75: bspeed = B75; break;
					case 110: bspeed = B110; break;
					case 134: bspeed = B134; break;
					case 150: bspeed = B150; break;
					case 200: bspeed = B200; break;
					case 300: bspeed = B300; break;
					case 600: bspeed = B600; break;
					case 1200: bspeed = B1200; break;
					case 1800: bspeed = B1800; break;
					case 2400: bspeed = B2400; break;
					case 4800: bspeed = B4800; break;
					case 9600: bspeed = B9600; break;
					case 19200: bspeed = B19200; break;
					case 38400: bspeed = B38400; break;
					case 57600: bspeed = B57600; break;
					case 115200: bspeed = B115200; break;
					case 230400: bspeed = B230400; break;
					default:
						printf("unknown speed: %d\n",speed);
						exit(1);
				}
				break;
		}
	}

	/* open the device */
	pf = open(device, O_RDWR);
	if (pf < 0)
		main_usage(2, "cannot open device", device);

	/* modify the port configuration */
	tcgetattr(pf, &pts);
	memcpy(&pots, &pts, sizeof (pots));
	init_comm(&pts, bspeed);
	tcsetattr(pf, TCSANOW, &pts);

	/* Now deal with the local terminal side */
	tcgetattr(STDIN_FILENO, &sts);
	memcpy(&sots, &sts, sizeof (sots));	/* to be used upon exit */
	init_stdin(&sts);
	tcsetattr(STDIN_FILENO, TCSANOW, &sts);

	/* set the signal handler to restore the old
	 * termios handler */
	sact.sa_handler = cleanup_termios;
	sigaction(SIGHUP, &sact, NULL);
	sigaction(SIGINT, &sact, NULL);
	sigaction(SIGPIPE, &sact, NULL);
	sigaction(SIGTERM, &sact, NULL);

	/* run thhe main program loop */
	mux_loop(pf);

	/* restore original terminal settings and exit */
	tcsetattr(pf, TCSANOW, &pots);
	tcsetattr(STDIN_FILENO, TCSANOW, &sots);

	exit(0);

}
