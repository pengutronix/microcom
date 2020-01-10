AC_INIT([microcom], [2019.01.0], [oss-tools@pengutronix.de])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([dist-xz])

AC_PREREQ([2.69])
AC_CONFIG_SRCDIR([commands_fsl_imx.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_SYS_LARGEFILE

# Checks for libraries.
AC_CHECK_LIB([readline], [readline])

# Checks for header files.
AC_CHECK_HEADERS([arpa/inet.h fcntl.h limits.h netdb.h netinet/in.h stdint.h stdlib.h string.h sys/ioctl.h sys/socket.h sys/time.h termios.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_CHECK_FUNCS([dup2 memset select socket strchr strerror strrchr strtol strtoul])

AC_ARG_ENABLE([can], [AS_HELP_STRING([--enable-can], [enable can mode @<:@default=check@:>@])],,
	[enable_can=check])

AS_IF([test "x$enable_can" != "xno"],
      [AC_CHECK_HEADERS([linux/can.h linux/can/raw.h],,
			[AS_IF([test "x$enable_can" != "xyes"], [enable_can=no; break], [AC_MSG_ERROR([can depends on Linux can headers])])])
       AS_IF([test "x$enable_can" = "xcheck"], [enable_can=yes])
      ])

AS_IF([test "x$enable_can" = "xyes"],
      [AC_DEFINE([USE_CAN], [1], [Define if can mode should be built-in])])

AM_CONDITIONAL([CAN], [test "x$enable_can" = "xyes"])

AC_CONFIG_FILES([Makefile])

AC_OUTPUT
