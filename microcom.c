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
#include <arpa/inet.h>
#include <arpa/telnet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <string.h>

int crnl_mapping;		//0 - no mapping, 1 mapping
int dolog = 0;			/* log active flag */
FILE *flog;			/* log file */
int pf = 0;			/* port file descriptor */
struct termios pots;		/* old port termios settings to restore */
struct termios sots;		/* old stdout/in termios settings to restore */

int telnet = 0;
struct ios_ops *ios;
int debug = 0;

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
	crnl_mapping = 0;
	pts->c_iflag &= ~ICRNL;
}

void init_stdin(struct termios *sts)
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

int serial_set_speed(int fd, speed_t speed)
{
	struct termios pts;	/* termios settings on port */

	tcgetattr(fd, &pts);
	cfsetospeed(&pts, speed);
	cfsetispeed(&pts, speed);
	tcsetattr(fd, TCSANOW, &pts);

	return 0;
}

int serial_set_flow(int fd, int flow)
{
	struct termios pts;	/* termios settings on port */
	tcgetattr(fd, &pts);

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

	tcsetattr(fd, TCSANOW, &pts);

	return 0;
}

struct ios_ops serial_ops = {
	.set_speed = serial_set_speed,
	.set_flow = serial_set_flow,
};

int telnet_set_speed(int fd, speed_t speed)
{

//	unsigned char buf1[] = {IAC, WILL , COM_PORT_OPTION};
	unsigned char buf2[] = {IAC, SB, COM_PORT_OPTION, SET_BAUDRATE_CS, 0, 0, 0, 0, IAC, SE};
	int *speedp = (int *)&buf2[4];

//	write(fd, buf1, 3);
	*speedp = htonl(flag_to_baudrate(speed));
	write(fd, buf2, 10);
}

int telnet_set_flow(int fd, int flow)
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
	write(fd, buf2, sizeof(buf2));

}

struct ios_ops telnet_ops = {
	.set_speed = telnet_set_speed,
	.set_flow = telnet_set_flow,
};

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
		"    -p devfile      use the specified serial port device (%s);\n"
		"    -s speed        use specified baudrate (%d)\n"
		"    -t host:port    work in telnet (rfc2217) mode\n"
		"microcom provides session logging in microcom.log file\n",
		DEFAULT_DEVICE, DEFAULT_BAUDRATE);
	fprintf(stderr, "Exitcode %d - %s %s\n\n", exitcode, str, dev);
	exit(exitcode);
}

/* restore original terminal settings on exit */
void cleanup_termios(int signal)
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

int main(int argc, char *argv[])
{
	struct termios pts;	/* termios settings on port */
	struct termios sts;	/* termios settings on stdout/in */
	struct sigaction sact;	/* used to initialize the signal handler */
	int opt,i, speed = DEFAULT_BAUDRATE;
	char *hostport;
	char *device = DEFAULT_DEVICE;

	struct option long_options[] = {
		{ "help", no_argument, 0, 'h' },
		{ "port", required_argument, 0, 'p'},
		{ "speed", required_argument, 0, 's'},
		{ "telnet", required_argument, 0, 't'},
		{ "debug", no_argument, 0, 'd' },
		{ 0, 0, 0, 0},
	};

	while ((opt = getopt_long(argc, argv, "hp:s:t:d", long_options, NULL)) != -1) {
		switch (opt) {
			case 'h':
				main_usage(1, "", "");
				exit(0);
			case 'p':
				device = optarg;
				break;
			case 's':
				speed = strtoul(optarg, NULL, 0);
				break;
			case 't':
				telnet = 1;
				hostport = optarg;
				break;
			case 'd':
				debug = 1;
		}
	}

	printf("*** Welcome to microcom ***\n");

	if (telnet) {
		int sock;
		struct sockaddr_in server_in;
		char *host = hostport;
		char *portstr;
		int port = 23;
		struct hostent *hp;

		ios = &telnet_ops;

		portstr = strchr(hostport, ':');
		if (portstr) {
			*portstr = 0;
			portstr++;
			port = atoi(portstr);
		}

		hp = gethostbyname(host);
		if (!hp) {
			perror("gethostbyname");
			exit(1);
		}

		host = inet_ntoa(*(struct in_addr*)(hp->h_addr_list[0]));
	
		memset(&server_in, 0, sizeof(server_in));     /* Zero out structure */
		server_in.sin_family      = AF_INET;             /* Internet address family */
		server_in.sin_addr.s_addr = inet_addr(host);   /* Server IP address */
		server_in.sin_port        = htons(port); /* Server port */

		sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock < 0) {
			printf("socket() failed\n");
			exit(1);
		}

		/* Establish the connection to the echo server */
		if (connect(sock, (struct sockaddr *) &server_in, sizeof(server_in)) < 0) {
			perror("connect");
			exit(1);
		}
		pf = sock;
		printf("connected to %s (port %d)\n", host, port);
	} else {
		ios = &serial_ops;

		/* open the device */
		pf = open(device, O_RDWR);
		if (pf < 0)
			main_usage(2, "cannot open device", device);

		/* modify the port configuration */
		tcgetattr(pf, &pts);
		memcpy(&pots, &pts, sizeof (pots));
		init_comm(&pts);
		tcsetattr(pf, TCSANOW, &pts);
		printf("connected to %s\n", device);
	}

	speed = baudrate_to_flag(speed);
		if (speed < 0)
			exit(1);

	ios->set_speed(pf, speed);

	ios->set_flow(pf, FLOW_NONE);

	printf("Escape character: Ctrl-\\\n");
	printf("Type the escape character followed by c to get to the menu or q to quit\n");

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

	if (!telnet) {
		/* restore original terminal settings and exit */
		tcsetattr(pf, TCSANOW, &pots);
		tcsetattr(STDIN_FILENO, TCSANOW, &sots);
	}

	exit(0);

}
