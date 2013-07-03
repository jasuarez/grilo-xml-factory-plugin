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
   fetch.c                    \
   fetch.h                    \
   log.c                      \
   log.h                      \
   dataref.c                  \
   dataref.h                  \
   expandable-string.c        \
   expandable-string.h

extdir               = $(GRL_PLUGINS_DIR)
xmlfactoryxmldir     = $(GRL_PLUGINS_DIR)
xmlfactoryxml_DATA   = $(XML_FACTORY_PLUGIN_ID).xml

xsd_DATA = $(XML_FACTORY_PLUGIN_ID).xsd
xsddir   = $(XML_FACTORY_SCHEMA_LOCATION)

# This lets us test the plugin without installing it,
# because grilo expects the .so and .xml files to be in
# the same directory:
copy-xml-to-libs-dir: libgrlxmlfactory.la
	cp -f $(srcdir)/$(xmlfactoryxml_DATA) $(builddir)/.libs/

all-local: copy-xml-to-libs-dir

EXTRA_DIST =               \
   $(xmlfactoryxml_DATA)   \
   $(xsd_DATA)

DIST_SUBDIRS = sources

MAINTAINERCLEANFILES =  \
   *.in                 \
   *~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
