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
#include <sys/socket.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>

int dolog;			/* log active flag */
FILE *flog;			/* log file */
static struct termios sots;	/* old stdout/in termios settings to restore */

struct ios_ops *ios;
int debug;

void init_terminal(void)
{
	struct termios sts;

	memcpy(&sts, &sots, sizeof (sots));     /* to be used upon exit */

	/* again, some arbitrary things */
	sts.c_iflag &= ~(IGNCR | INLCR | ICRNL);
	sts.c_iflag |= IGNBRK;
	sts.c_lflag &= ~ISIG;
	sts.c_cc[VMIN] = 1;
	sts.c_cc[VTIME] = 0;
	sts.c_lflag &= ~ICANON;
	/* no local echo: allow the other end to do the echoing */
	sts.c_lflag &= ~(ECHO | ECHOCTL | ECHONL);

	tcsetattr(STDIN_FILENO, TCSANOW, &sts);
}

void restore_terminal(void)
{
	tcsetattr(STDIN_FILENO, TCSANOW, &sots);
}

speed_t baudrate_to_flag(int speed)
{
	switch(speed) {
	case 50: return B50;
	case 75: return B75;
	case 110: return B110;
	case 134: return B134;
	case 150: return B150;
	case 200: return B200;
	case 300: return B300;
	case 600: return B600;
	case 1200: return B1200;
	case 1800: return B1800;
	case 2400: return B2400;
	case 4800: return B4800;
	case 9600: return B9600;
	case 19200: return B19200;
	case 38400: return B38400;
	case 57600: return B57600;
	case 115200: return B115200;
	case 230400: return B230400;
	default:
		printf("unknown speed: %d\n",speed);
		return -1;
	}
}

int flag_to_baudrate(speed_t speed)
{
	switch(speed) {
	case B50: return 50;
	case B75: return 75;
	case B110: return 110;
	case B134: return 134;
	case B150: return 150;
	case B200: return 200;
	case B300: return 300;
	case B600: return 600;
	case B1200: return 1200;
	case B1800: return 1800;
	case B2400: return 2400;
	case B4800: return 4800;
	case B9600: return 9600;
	case B19200: return 19200;
	case B38400: return 38400;
	case B57600: return 57600;
	case B115200: return 115200;
	case B230400: return 230400;
	default:
		printf("unknown speed: %d\n",speed);
		return -1;
	}
}

void microcom_exit(int signal)
{
	printf("exiting\n");

	/* close the log file first */
	if (dolog) {
		fflush(flog);
		fclose(flog);
	}

	ios->exit(ios);
	tcsetattr(STDIN_FILENO, TCSANOW, &sots);

	exit(0);
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
void main_usage(int exitcode, char *str, char *dev)
{
	fprintf(stderr, "Usage: microcom [options]\n"
		" [options] include:\n"
		"    -p devfile                  use the specified serial port device (%s);\n"
		"    -s speed                    use specified baudrate (%d)\n"
		"    -t host:port                work in telnet (rfc2217) mode\n"
		"    -c interface:rx_id:tx_id    work in CAN mode\n"
		"                                default: (%s:%x:%x)\n"
		"    -f                          ignore existing lock file\n"
		"    -d                          output debugging info\n"
		DEFAULT_DEVICE, DEFAULT_BAUDRATE,
		DEFAULT_CAN_INTERFACE, DEFAULT_CAN_ID, DEFAULT_CAN_ID);
	fprintf(stderr, "Exitcode %d - %s %s\n\n", exitcode, str, dev);
	exit(exitcode);
}

int opt_force = 0;
int current_speed = DEFAULT_BAUDRATE;
int current_flow = FLOW_NONE;

int main(int argc, char *argv[])
{
	struct sigaction sact;  /* used to initialize the signal handler */
	int opt;
	char *hostport = NULL;
	int telnet = 0, can = 0;
	char *interfaceid = NULL;
	char *device = DEFAULT_DEVICE;
	speed_t flag;

	struct option long_options[] = {
		{ "help", no_argument, 0, 'h' },
		{ "port", required_argument, 0, 'p'},
		{ "speed", required_argument, 0, 's'},
		{ "telnet", required_argument, 0, 't'},
		{ "can", required_argument, 0, 'c'},
		{ "debug", no_argument, 0, 'd' },
		{ "force", no_argument, 0, 'f' },
		{ 0, 0, 0, 0},
	};

	while ((opt = getopt_long(argc, argv, "hp:s:t:c:df", long_options, NULL)) != -1) {
		switch (opt) {
			case 'h':
			case '?':
				main_usage(1, "", "");
				exit(0);
			case 'p':
				device = optarg;
				break;
			case 's':
				current_speed = strtoul(optarg, NULL, 0);
				break;
			case 't':
				telnet = 1;
				hostport = optarg;
				break;
			case 'c':
				can = 1;
				interfaceid = optarg;
				break;
			case 'f':
				opt_force = 1;
				break;
			case 'd':
				debug = 1;
		}
	}

	commands_init();
	commands_fsl_imx_init();

	if (telnet && can)
		main_usage(1, "", "");

	if (telnet)
		ios = telnet_init(hostport);
	else if (can)
		ios = can_init(interfaceid);
	else
		ios = serial_init(device);

	if (!ios)
		exit(1);

	flag = baudrate_to_flag(current_speed);
	if (flag < 0)
		exit(1);

	current_flow = FLOW_NONE;
	ios->set_speed(ios, flag);
	ios->set_flow(ios, current_flow);

	printf("Escape character: Ctrl-\\\n");
	printf("Type the escape character followed by c to get to the menu or q to quit\n");

	/* Now deal with the local terminal side */
	tcgetattr(STDIN_FILENO, &sots);
	init_terminal();

	/* set the signal handler to restore the old
	 * termios handler */
	sact.sa_handler = &microcom_exit;
	sigaction(SIGHUP, &sact, NULL);
	sigaction(SIGINT, &sact, NULL);
	sigaction(SIGPIPE, &sact, NULL);
	sigaction(SIGTERM, &sact, NULL);
	sigaction(SIGQUIT, &sact, NULL);

	/* run thhe main program loop */
	mux_loop(ios);

	microcom_exit(0);

	return 0;
}
