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

void cook_buf(int fd, char *buf, int num); /* microcom.c */ 
void mux_loop(int pf); /* mux.c */

typedef enum {
  S_TIMEOUT,		/* timeout */
  S_DTE,		/* incoming data coming from kbd */
  S_DCE,		/* incoming data from serial port */
  S_MAX			/* not used - just for checking */
} S_ORIGINATOR;

int script_process(S_ORIGINATOR orig, char* buf, int size); /* script.c */
void script_init(char* s); /* script.c */
void mux_clear_sflag(void); /* mus.c */
void cleanup_termios(int signal);
void init_stdin(struct termios *sts);
void init_comm(struct termios *pts, int speed);
void main_usage(int exitcode, char *str, char *dev);
   
#endif /* MICROCOM_H */






