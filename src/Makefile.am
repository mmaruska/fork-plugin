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


# Changed by: Michal Maruska (C) 2010



# this is obnoxious:
# -module lets us name the module exactly how we want
# -avoid-version prevents gratuitous .0.0.0 version numbers on the end
# _ladir passes a dummy rpath to libtool so the thing will actually link
# TODO: -nostdlib/-Bstatic/-lgcc platform magic, not installing the .a, etc.
@DRIVER_NAME@_la_LTLIBRARIES = @DRIVER_NAME@.la
@DRIVER_NAME@_la_LDFLAGS = -module -avoid-version --no-export-dynamic
@DRIVER_NAME@_ladir = @inputdir@
#/usr/lib/xorg/modules

# queue.cpp
@DRIVER_NAME@_la_SOURCES = @DRIVER_NAME@.cpp configure.cpp history.cpp fork.h circular.h queue.h config.h


@DRIVER_NAME@_CFLAGS = @XORG_CFLAGS@ -I../include/
CFLAGS += $(@DRIVER_NAME@_CFLAGS)
CPPFLAGS = $(CFLAGS)
