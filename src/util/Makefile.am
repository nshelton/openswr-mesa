# Copyright © 2014 Intel Corporation
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice (including the next
# paragraph) shall be included in all copies or substantial portions of the
# Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

SUBDIRS = . tests/hash_table

include Makefile.sources

noinst_LTLIBRARIES = libmesautil.la

AM_CPPFLAGS = \
	-I$(top_srcdir)/include

libmesautil_la_CPPFLAGS = \
	$(DEFINES) \
	-I$(top_srcdir)/include \
	-I$(top_srcdir)/src \
	-I$(top_srcdir)/src/mapi \
	-I$(top_srcdir)/src/mesa \
	-I$(top_srcdir)/src/gallium/include \
	-I$(top_srcdir)/src/gallium/auxiliary \
	$(VISIBILITY_CFLAGS) \
	$(MSVC2008_COMPAT_CFLAGS)

libmesautil_la_SOURCES = \
	$(MESA_UTIL_FILES) \
	$(MESA_UTIL_GENERATED_FILES)

roundeven_test_LDADD = -lm

check_PROGRAMS = u_atomic_test roundeven_test
TESTS = $(check_PROGRAMS)

BUILT_SOURCES = $(MESA_UTIL_GENERATED_FILES)
CLEANFILES = $(BUILT_SOURCES)
EXTRA_DIST = format_srgb.py SConscript

PYTHON_GEN = $(AM_V_GEN)$(PYTHON2) $(PYTHON_FLAGS)

format_srgb.c: format_srgb.py
	$(PYTHON_GEN) $(srcdir)/format_srgb.py > $@
