/***************************************************************************
** File: microcom.h
** Description: the main header file for microcom project
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
** Rev. 1.02 - June 2000
****************************************************************************/
#ifndef MICROCOM_H
#define MICROCOM_H
#include <sys/time.h>
#include <sys/types.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <assert.h>

#define MAX_SCRIPT_NAME 20 /* maximum length of the name of the script file */
#define MAX_DEVICE_NAME 20 /* maximum length of the name of the /dev comm port driver */

#define DEFAULT_BAUDRATE 115200
#define DEFAULT_DEVICE "/dev/ttyS0"

typedef enum {
	S_TIMEOUT,		/* timeout */
	S_DTE,		/* incoming data coming from kbd */
	S_DCE,		/* incoming data from serial port */
	S_MAX			/* not used - just for checking */
} S_ORIGINATOR;

struct ios_ops {
	int (*set_speed)(struct ios_ops *, speed_t speed);
#define FLOW_NONE	0
#define FLOW_SOFT	1
#define FLOW_HARD	2
	int (*set_flow)(struct ios_ops *, int flow);
	void (*exit)(struct ios_ops *);
	int fd;
};

void cook_buf(struct ios_ops *, unsigned char *buf, int num); /* microcom.c */ 
void mux_loop(struct ios_ops *); /* mux.c */

struct ios_ops *telnet_init(char *hostport);
struct ios_ops *serial_init(char *dev);

void microcom_exit(int signal);

void main_usage(int exitcode, char *str, char *dev);

int flag_to_baudrate(speed_t speed);
speed_t baudrate_to_flag(int speed);

extern struct ios_ops *ios;
extern int debug;
extern int dolog;
extern FILE *flog;

#define dprintf(fmt,args...)  ({ if (debug) printf (fmt ,##args); })

/* RFC2217 */
#define COM_PORT_OPTION		 44
#define SET_BAUDRATE_CS		  1
#define SET_DATASIZE_CS		  2
#define SET_PARITY_CS		  3
#define SET_STOPSIZE_CS		  4
#define SET_CONTROL_CS		  5
#define NOTIFY_LINESTATE_CS	  6
#define NOTIFY_MODEMSTATE_CS	  7
#define FLOWCONTROL_SUSPEND_CS	  8
#define FLOWCONTROL_RESUME_CS	  9
#define SET_LINESTATE_MASK_CS	 10
#define SET_MODEMSTATE_MASK_CS	 11
#define PURGE_DATA_CS		 12
#define SET_BAUDRATE_SC		101
#define SET_DATASIZE_SC		102
#define SET_PARITY_SC		103
#define SET_STOPSIZE_SC		104
#define SET_CONTROL_SC		105
#define NOTIFY_LINESTATE_SC	106
#define NOTIFY_MODEMSTATE_SC	107
#define FLOWCONTROL_SUSPEND_SC	108
#define FLOWCONTROL_RESUME_SC	109
#define SET_LINESTATE_MASK_SC	110
#define SET_MODEMSTATE_MASK_SC	111
#define PURGE_DATA_SC		112

#endif /* MICROCOM_H */






