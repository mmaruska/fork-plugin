#  Copyright 2005 Adam Jackson.
#
#  Permission is hereby granted, free of charge, to any person obtaining a
#  copy of this software and associated documentation files (the "Software"),
#  to deal in the Software without restriction, including without limitation
#  on the rights to use, copy, modify, merge, publish, distribute, sub
#  license, and/or sell copies of the Software, and to permit persons to whom
#  the Software is furnished to do so, subject to the following conditions:
#
#  The above copyright notice and this permission notice (including the next
#  paragraph) shall be included in all copies or substantial portions of the
#  Software.
#
#  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#  FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.  IN NO EVENT SHALL
#  ADAM JACKSON BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
#  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
#  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
#
# Process this file with autoconf to produce a configure script

# Initialize Autoconf
AC_PREREQ(2.60)
AC_INIT([fork-plugin],
        [2.8.9],
        [https://bugs.freedesktop.org/enter_bug.cgi?product=xorg],
        [fork-plugin])
AC_CONFIG_SRCDIR([Makefile.am])
AC_CONFIG_HEADERS([config.h])

# Initialize Automake
AM_INIT_AUTOMAKE([foreign dist-bzip2])
AM_MAINTAINER_MODE

# Initialize libtool
AC_DISABLE_STATIC
AC_PROG_LIBTOOL
AC_PROG_CXX

# Initialize Autoheader
AH_TOP([#include "xorg-server.h"])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Initialize X.Org macros
m4_ifndef([XORG_MACROS_VERSION],
          [m4_fatal([must install xorg-macros 1.4 or later before running autoconf/autogen])])
XORG_MACROS_VERSION(1.4)
XORG_DEFAULT_OPTIONS

# Define a configure option for an alternate input module directory
AC_ARG_WITH(xorg-module-dir,
            AS_HELP_STRING([--with-xorg-module-dir=DIR],
                           [Default xorg module directory [[default=$libdir/xorg/modules]]]),
            [moduledir="$withval"],
            [moduledir="$libdir/xorg/modules"])
inputdir=${moduledir}
# /input
AC_SUBST(inputdir)


# Checks for extensions
# XORG_DRIVER_CHECK_EXT(RANDR, randrproto)
#XORG_DRIVER_CHECK_EXT(XKB, kbproto)
# XORG_DRIVER_CHECK_EXT(XINPUT, inputproto)

# Checks for pkg-config packages
PKG_CHECK_MODULES(XORG, [xorg-server >= 1.0.99.901] xproto $REQUIRED_MODULES)

# includedir=$(pkg-config --variable=sdkdir xorg-server)
# AC_SUBST(includedir)

# Define an Automake variable for the driver name
DRIVER_NAME=fork
AC_SUBST([DRIVER_NAME])
AC_SUBST([XORG_CFLAGS])

AC_CONFIG_FILES([Makefile
		  include/Makefile
		  src/Makefile])
#                 man/Makefile


predep_objects=""
postdep_objects=""

AC_OUTPUT

