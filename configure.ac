#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ([2.68])
AC_INIT([utfout], [0.0.1], [james.hunt@ubuntu.com])
AC_COPYRIGHT([Copyright (C) 2012 James Hunt <james.hunt@ubuntu.com>])

AC_CONFIG_SRCDIR([src/utfout.c])
AC_CONFIG_HEADERS([config.h])
AC_CONFIG_AUX_DIR([config])

# Checks for programs.
AC_PROG_CC

AM_GNU_GETTEXT([external])

# Checks for libraries.

# Checks for header files.
AC_HEADER_STDC
#AC_CHECK_HEADERS([fcntl.h langinfo.h locale.h paths.h stdlib.h string.h unistd.h wchar.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T

# Checks for library functions.
AC_CHECK_FUNCS([nl_langinfo setlocale strdup])

AM_INIT_AUTOMAKE
AC_OUTPUT(Makefile src/Makefile po/Makefile.in)
