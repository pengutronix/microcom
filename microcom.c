// SPDX-License-Identifier: GPL-2.0-only
// SPDX-FileCopyrightText: 1999 Anca and Lucian Jurubita <ljurubita@hotmail.com>.
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
#include "compat.h"

static struct termios sots; /* old stdout/in termios settings to restore */

struct ios_ops *ios;
int debug = 0;

void init_terminal(void)
{
	struct termios sts;

	memcpy(&sts, &sots, sizeof(sots)); /* to be used upon exit */

	/* Implement what `stty raw` does. */
	sts.c_iflag &= ~(IGNBRK | BRKINT | IGNPAR | PARMRK | INPCK |
			 ISTRIP | INLCR | IGNCR | ICRNL | IXON | IXOFF |
			 IUCLC | IXANY | IMAXBEL);
	sts.c_lflag &= ~(ICANON | ISIG | XCASE);
	sts.c_oflag &= ~OPOST;
	sts.c_cc[VMIN] = 1;
	sts.c_cc[VTIME] = 0;

	/* no local echo: allow the other end to do the echoing */
	sts.c_lflag &= ~ECHO;

	tcsetattr(STDIN_FILENO, TCSANOW, &sts);
}

void restore_terminal(void)
{
	tcsetattr(STDIN_FILENO, TCSANOW, &sots);
}


void microcom_exit(int signal)
{
	write(1, "exiting\n", 8);

	ios->exit(ios);
	tcsetattr(STDIN_FILENO, TCSANOW, &sots);

	if (signal)
		_Exit(0);
}

/*
 * Main functions
 ********************************************************************
 * static void help_usage(int exitcode, char *error, char *addl)
 *      help with running the program
 *      - exitcode - to be returned when the program is ended
 *      - error - error string to be printed
 *      - addl - another error string to be printed
 * static void cleanup_termios(int signal)
 *      signal handler to restore terminal set befor exit
 * int main(int argc, char *argv[]) -
 *      main program function
 */
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
		"    -a, --answerback=<str>               specify the answerback string sent as response to\n"
		"                                         an ENQ (ASCII 0x05) Character\n"
		"    -e, --escape-char=<chr>              escape charater to use with Ctrl (%c)\n"
		"    -v, --version                        print version string\n"
		"    -h, --help                           This help\n",
		DEFAULT_DEVICE, DEFAULT_BAUDRATE,
		DEFAULT_CAN_INTERFACE, DEFAULT_CAN_ID, DEFAULT_CAN_ID,
		DEFAULT_ESCAPE_CHAR);
	fprintf(stderr, "Exitcode %d - %s %s\n\n", exitcode, str, dev);
	exit(exitcode);
}

int opt_force = 0;
unsigned long current_speed = DEFAULT_BAUDRATE;
int current_flow = FLOW_NONE;
int listenonly = 0;
char escape_char = DEFAULT_ESCAPE_CHAR;

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
		{ "help", no_argument, NULL, 'h' },
		{ "port", required_argument, NULL, 'p' },
		{ "speed", required_argument, NULL, 's' },
		{ "telnet", required_argument, NULL, 't' },
		{ "can", required_argument, NULL, 'c' },
		{ "debug", no_argument, NULL, 'd' },
		{ "force", no_argument, NULL, 'f' },
		{ "logfile", required_argument, NULL, 'l' },
		{ "listenonly", no_argument, NULL, 'o' },
		{ "answerback", required_argument, NULL, 'a' },
		{ "version", no_argument, NULL, 'v' },
		{ 0 },
	};

	while ((opt = getopt_long(argc, argv, "hp:s:t:c:dfl:oi:a:e:v", long_options, NULL)) != -1) {
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
		case 'e':
			if (strlen(optarg) != 1) {
				fprintf(stderr, "Option -e requires a single character argument.\n");
				exit(EXIT_FAILURE);
			}
			escape_char = *optarg;
			break;
		}
	}

	if (optind < argc)
		main_usage(1, "", "");

	if (answerback) {
		ret = asprintf(&answerback, "%s\n", answerback);
		if (ret < 0)
			exit(1);
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

	ret = ios->set_speed(ios, current_speed);
	if (ret)
		goto cleanup_ios;

	current_flow = FLOW_NONE;
	ios->set_flow(ios, current_flow);

	if (!listenonly) {
		printf("Escape character: Ctrl-%c\n", escape_char);
		printf("Type the escape character to get to the prompt.\n");

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
	}

	/* run the main program loop */
	ret = mux_loop(ios);

	if (!listenonly)
		tcsetattr(STDIN_FILENO, TCSANOW, &sots);

cleanup_ios:
	ios->exit(ios);

	exit(ret ? 1 : 0);
}
