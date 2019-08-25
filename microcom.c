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
#define _GNU_SOURCE
#include "microcom.h"

#include <unistd.h>
#include <getopt.h>
#include <sys/socket.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "config.h"

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

int baudrate_to_flag(int speed, speed_t *flag)
{
	size_t i;

	/* possible optimisation: binary search for speed */
	for (i = 0; i < ARRAY_SIZE(bd_to_flg); ++i)
		if (bd_to_flg[i].speed == speed) {
			*flag = bd_to_flg[i].flag;
			return 0;
		}

	printf("unknown speed: %d\n", speed);
	return -1;
}

int flag_to_baudrate(speed_t speed)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(bd_to_flg); ++i)
		if (bd_to_flg[i].flag == speed)
			return bd_to_flg[i].speed;

	printf("unknown speed flag: 0x%x\n", (unsigned)speed);
	return -1;
}

void microcom_exit(int signal)
{
	write(1, "exiting\n", 8);

	ios->exit(ios);
	tcsetattr(STDIN_FILENO, TCSANOW, &sots);

	if (signal)
		_Exit(0);
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
		"    -p, --port=<devfile>                 use the specified serial port device (%s);\n"
		"    -s, --speed=<speed>                  use specified baudrate (%d)\n"
		"    -t, --telnet=<host:port>             work in telnet (rfc2217) mode\n"
		"    -c, --can=<interface:rx_id:tx_id>    work in CAN mode\n"
		"                                         default: (%s:%x:%x)\n"
		"    -f, --force                          ignore existing lock file\n"
		"    -d, --debug                          output debugging info\n"
		"    -l, --logfile=<logfile>              log output to <logfile>\n"
		"    -o, --listenonly                     Do not modify local terminal, do not send input\n"
		"                                         from stdin\n"
		"    -a,  --answerback=<str>              specify the answerback string sent as response to\n"
		"                                         an ENQ (ASCII 0x05) Character\n"
		"    -v, --version                        print version string\n"
		"    -h, --help                           This help\n",
		DEFAULT_DEVICE, DEFAULT_BAUDRATE,
		DEFAULT_CAN_INTERFACE, DEFAULT_CAN_ID, DEFAULT_CAN_ID);
	fprintf(stderr, "Exitcode %d - %s %s\n\n", exitcode, str, dev);
	exit(exitcode);
}

int opt_force = 0;
unsigned long current_speed = DEFAULT_BAUDRATE;
int current_flow = FLOW_NONE;
int listenonly = 0;

int main(int argc, char *argv[])
{
	struct sigaction sact = {0};  /* used to initialize the signal handler */
	int opt, ret;
	char *hostport = NULL;
	int telnet = 0, can = 0;
	char *interfaceid = NULL;
	char *device = DEFAULT_DEVICE;
	char *logfile = NULL;

	struct option long_options[] = {
		{ "help", no_argument, 0, 'h' },
		{ "port", required_argument, 0, 'p'},
		{ "speed", required_argument, 0, 's'},
		{ "telnet", required_argument, 0, 't'},
		{ "can", required_argument, 0, 'c'},
		{ "debug", no_argument, 0, 'd' },
		{ "force", no_argument, 0, 'f' },
		{ "logfile", required_argument, 0, 'l'},
		{ "listenonly", no_argument, 0, 'o'},
		{ "answerback", required_argument, 0, 'a'},
		{ "version", no_argument, 0, 'v' },
		{ 0, 0, 0, 0},
	};

	while ((opt = getopt_long(argc, argv, "hp:s:t:c:dfl:oi:a:v", long_options, NULL)) != -1) {
		switch (opt) {
			case '?':
				main_usage(1, "", "");
				break;
			case 'h':
				main_usage(0, "", "");
				break;
			case 'v':
				printf("%s\n", PACKAGE_VERSION);
				exit(EXIT_SUCCESS);
				break;
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
				break;
			case 'l':
				logfile = optarg;
				break;
			case 'o':
				listenonly = 1;
				break;
			case 'a':
				answerback = optarg;
				break;
		}
	}

	if (answerback) {
		ret = asprintf(&answerback, "%s\n", answerback);
		if (ret < 0)
			exit (1);
	}

	commands_init();
	commands_fsl_imx_init();

	if (telnet && can)
		main_usage(1, "", "");

	if (telnet)
		ios = telnet_init(hostport);
	else if (can) {
#ifdef USE_CAN
		ios = can_init(interfaceid);
#else
		fprintf(stderr, "CAN mode not supported\n");
		exit(EXIT_FAILURE);
#endif
	} else
		ios = serial_init(device);

	if (!ios)
		exit(1);

	if (logfile) {
		ret = logfile_open(logfile);
		if (ret < 0)
			exit(1);
	}

	current_flow = FLOW_NONE;
	if (ios->set_speed(ios, current_speed))
		exit(EXIT_FAILURE);

	ios->set_flow(ios, current_flow);

	if (!listenonly) {
		printf("Escape character: Ctrl-\\\n");
		printf("Type the escape character to get to the prompt.\n");

		/* Now deal with the local terminal side */
		tcgetattr(STDIN_FILENO, &sots);
		init_terminal();

		/* set the signal handler to restore the old
		 * termios handler */
		sact.sa_handler = &microcom_exit;
		sigemptyset(&sact.sa_mask);
		sigaction(SIGHUP, &sact, NULL);
		sigaction(SIGINT, &sact, NULL);
		sigaction(SIGPIPE, &sact, NULL);
		sigaction(SIGTERM, &sact, NULL);
		sigaction(SIGQUIT, &sact, NULL);
	}

	/* run the main program loop */
	ret = mux_loop(ios);

	ios->exit(ios);

	if (!listenonly)
		tcsetattr(STDIN_FILENO, TCSANOW, &sots);

	exit(ret ? 1 : 0);
}
