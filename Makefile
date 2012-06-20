#/******************************************************************
#** File: Makefile
#** Description: the makefile for microcom project
#**
#** Copyright (C)1999 Anca and Lucian Jurubita <ljurubita@hotmail.com>.
#** All rights reserved.
#****************************************************************************
#** This program is free software; you can redistribute it and/or
#** modify it under the terms of the GNU General Public License
#** as published by the Free Software Foundation; either version 2
#** of the License, or (at your option) any later version.
#**
#** This program is distributed in the hope that it will be useful,
#** but WITHOUT ANY WARRANTY; without even the implied warranty of
#** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#** GNU General Public License for more details at www.gnu.org
#****************************************************************************
#** Rev. 0.9 - Sept. 1999
#** Rev. 0.91 - Jan. 2000 - minor fixes, compiled under Mandrake 6.0
#****************************************************************************/

CFLAGS		+= -Wall -O2 -g
LDLIBS		+= -lreadline -lpthread
CPPFLAGS	+= -DPKG_VERSION="\"2012.06.0\"" -DPF_CAN=29 -DAF_CAN=PF_CAN


microcom: microcom.o mux.o serial.o telnet.o can.o commands.o parser.o commands_fsl_imx.o

mux.o: mux.c microcom.h

microcom.o: microcom.c microcom.h

serial.o: serial.c microcom.h

telnet.o: telnet.c microcom.h

can.o: can.c microcom.h

commands.o: telnet.o microcom.h

parser.o: parser.c microcom.h

commands_fsl_imx.o: commands_fsl_imx.c microcom.h

clean:
	rm -f *.o microcom
