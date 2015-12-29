#
# Makefile.am
#
# Author: Juan A. Suarez Romero <jasuarez@igalia.com>
#
# Copyright (C) 2013 Igalia S.L. All rights reserved.

SUBDIRS = sources

ext_LTLIBRARIES = libgrlxmlfactory.la

libgrlxmlfactory_la_CFLAGS =                                            \
   $(DEPS_CFLAGS)                                                       \
   -DG_LOG_DOMAIN=\"GrlXmlFactory\"                                     \
   -DXML_FACTORY_SOURCE_LOCATION=\"@XML_FACTORY_SOURCE_LOCATION@\"      \
   -DXML_FACTORY_SCHEMA_LOCATION=\"@XML_FACTORY_SCHEMA_LOCATION@\"

libgrlxmlfactory_la_LIBADD =  \
   $(DEPS_LIBS)

libgrlxmlfactory_la_LDFLAGS = \
   -no-undefined              \
   -module                    \
   -avoid-version

libgrlxmlfactory_la_SOURCES = \
   grl-xml-factory.c          \
   grl-xml-factory.h          \
   json-ghashtable.c          \
   json-ghashtable.h          \
   fetch.c                    \
   fetch.h                    \
   log.c                      \
   log.h                      \
   dataref.c                  \
   dataref.h                  \
   expandable-string.c        \
   expandable-string.h

extdir               = $(GRL_PLUGINS_DIR)

xsd_DATA = $(XML_FACTORY_PLUGIN_ID).xsd
xsddir   = $(XML_FACTORY_SCHEMA_LOCATION)

EXTRA_DIST = $(xsd_DATA)

DIST_SUBDIRS = sources

MAINTAINERCLEANFILES =  \
   *.in                 \
   *~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
