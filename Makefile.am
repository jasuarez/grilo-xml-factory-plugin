#
## Makefile.am
#
## Author: Juan A. Suarez Romero <jasuarez@igalia.com>
#
## Copyright (C) 2013 Igalia S.L. All rights reserved.

include $(top_srcdir)/release.mk

SUBDIRS = src

MAINTAINERCLEANFILES =  \
   aclocal.m4           \
   compile              \
   config.guess         \
   config.sub           \
   configure            \
   depcomp              \
   install-sh           \
   ltmain.sh            \
   missing              \
   libtool              \
   stamp-h1             \
   Makefile.in          \
   *.make               \
   mkinstalldirs        \
   *~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)

distclean-local:
	-rm -rf autom4te.cache

dist-hook:
