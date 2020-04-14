AUTOMAKE_OPTIONS = foreign
ACLOCAL_AMFLAGS = -I m4 --install

EXTRA_DIST = DCO README.md

bin_PROGRAMS = microcom
microcom_SOURCES = commands.c commands_fsl_imx.c microcom.c mux.c parser.c serial.c telnet.c
if CAN
microcom_SOURCES += can.c
endif

dist_man1_MANS = microcom.1

noinst_HEADERS = microcom.h
