#
# configure.ac
#
# Author: Juan A. Suarez Romero <jasuarez@igalia.com>
#
# Copyright (C) 2013 Igalia S.L.. All rights reserved.

m4_define([prj_name],    [grilo-xml-factory-plugin])
m4_define([prj_version], [0.3.0])

AC_INIT([prj_name],
        [prj_version],
        [https://github.com/jasuarez/grilo-xml-factory-plugin/issues/new],
        [prj_name],
        [https://github.com/jasuarez/grilo-xml-factory-plugin])

AM_INIT_AUTOMAKE([foreign])

AC_CONFIG_SRCDIR([src])

AC_CONFIG_HEADERS(config.h)

AC_PROG_CC
AC_PROG_INSTALL
AC_DISABLE_STATIC

if test -z "$PKG_CONFIG" ; then
	PKG_CONFIG="pkg-config"
	if test -n "$host" ; then
		PKG_CONFIG=$host-$PKG_CONFIG
	fi
fi

LT_PREREQ([2.2.6])
LT_INIT

AM_PROG_CC_C_O

AC_HEADER_STDC

m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

# ----------------------------------------------------------
# ENVIRONMENT CONFIGURATION
# ----------------------------------------------------------

GNOME_COMPILE_WARNINGS([maximum])
CFLAGS+=" ${WARN_CFLAGS} -Wmissing-declarations -std=c99 "

GNOME_MAINTAINER_MODE_DEFINES

# ----------------------------------------------------------
# DEBUG SUPPORT
# ----------------------------------------------------------

AC_ARG_ENABLE(debug,
        AC_HELP_STRING([--enable-debug],
                [include debug symbols (default: no)]),,
        [enable_debug=no])

if test "x$enable_debug" = "xyes"; then
	CFLAGS="$CFLAGS -g3 -O0"
fi

# ----------------------------------------------------------
# DEFINITIONS
# ----------------------------------------------------------

# Plugins directory

GRL_PLUGINS_DIR=`${PKG_CONFIG} --variable=plugindir grilo-0.3`
AC_SUBST(GRL_PLUGINS_DIR)
AC_DEFINE_UNQUOTED(GRL_PLUGINS_DIR, "$GRL_PLUGINS_DIR", [Plugins directory])

# ----------------------------------------------------------
# DEPENDENCIES
# ----------------------------------------------------------

GLIB_MIN_REQ=2.28
GRILO_MIN_REQ=0.3.0

PKG_CHECK_MODULES([DEPS],
        glib-2.0 >= $GLIB_MIN_REQ
        gmodule-2.0
        gobject-2.0
        grilo-0.3 >= $GRILO_MIN_REQ
        grilo-net-0.3 
        json-glib-1.0
        libxml-2.0
        lua5.2
        rest-0.7)

GLIB_COMPILE_RESOURCES=`$PKG_CONFIG --variable glib_compile_resources gio-2.0`
AC_SUBST(GLIB_COMPILE_RESOURCES)

XML_FACTORY_PLUGIN_ID="grl-xml-factory"
AC_SUBST(XML_FACTORY_PLUGIN_ID)
AC_DEFINE_UNQUOTED([XML_FACTORY_PLUGIN_ID], ["$XML_FACTORY_PLUGIN_ID"], [XML Source Factory plugin ID])

XML_FACTORY_SOURCE_LOCATION="grilo-plugins/${XML_FACTORY_PLUGIN_ID}"
AC_SUBST(XML_FACTORY_SOURCE_LOCATION)

XML_FACTORY_SCHEMA_LOCATION="${datadir}/${XML_FACTORY_SOURCE_LOCATION}"
AC_SUBST(XML_FACTORY_SCHEMA_LOCATION)

# ----------------------------------------------------------
# TESTS COVERAGE
# ----------------------------------------------------------

AC_ARG_ENABLE(gcov,
        AC_HELP_STRING([--enable-gcov],
                [enable coverage testing (default: no)]),
        [enable_gcov=$enableval],
        [enable_gcov=no])

if test "x$enable_gcov" = "xyes"; then
   AC_PATH_PROG([enable_gcov], [lcov], [no])
   if test "x$enable_gcov" = "xno"; then
      AC_MSG_ERROR([You need lcov to build with coverage testing])
   elif test "x$GCC" = "xno"; then
      AC_MSG_ERROR([You need to use gcc to build with coverage testing])
   else
      enable_gcov=yes
      CFLAGS="$CFLAGS -g -O0 -fprofile-arcs -ftest-coverage"
      LDFLAGS="$LDFLAGS -lgcov"
   fi
fi

AM_CONDITIONAL([GCOV_ENABLED], [test "x$enable_gcov" = "xyes"])

# ----------------------------------------------------------
# WORKAROUNDS
# ----------------------------------------------------------

AC_SUBST([abs_top_builddir])
AC_SUBST([builddir])

# ----------------------------------------------------------
# OUTPUT
# ----------------------------------------------------------

AC_CONFIG_FILES([
  Makefile
  src/Makefile
  src/sources/Makefile
  tests/Makefile
  tests/sources/xml-test-log.xml
])

AC_OUTPUT
