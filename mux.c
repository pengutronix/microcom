/***************************************************************************
** File: mux.c
** Description: the main program loop
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
****************************************************************************/
#include "microcom.h"

#define SCRIPT_DELAY 1
#define BUFSIZE 1024

extern int dolog;
extern FILE *flog;

/* main program loop */
void
mux_loop(int pf)
{
	fd_set ready;		/* used for select */
	int i = 0;		/* used in the multiplex loop */
	int done = 0;
	char buf[BUFSIZE];
	struct timeval tv;

	tv.tv_sec = SCRIPT_DELAY;
	tv.tv_usec = 0;

	do {			/* forever */
		FD_ZERO(&ready);
		FD_SET(STDIN_FILENO, &ready);
		FD_SET(pf, &ready);

		select(pf + 1, &ready, NULL, NULL, NULL);

		if (FD_ISSET(pf, &ready)) {
			/* pf has characters for us */
			i = read(pf, buf, BUFSIZE);
			if (i > 0) {
				write(STDOUT_FILENO, buf, i);
				if (dolog) {
					fwrite(buf, 1, i, flog);
					fflush(flog);
				}
			} else
				done = 1;
		}
		/* if */
		if (FD_ISSET(STDIN_FILENO, &ready)) {
			/* standard input has characters for us */
			i = read(STDIN_FILENO, buf, BUFSIZE);
			if (i > 0)
				cook_buf(pf, buf, i);
			else
				done = 1;
		}		/* if */
	} while (!done);	/* do */
}
