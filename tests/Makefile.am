#
# Makefile.am
#
# Author: Iago Toral <itoral@igalia.com>
#
# Copyright (C) 2013 Igalia S.L.

include $(top_srcdir)/gtester.mk

INCLUDES = @DEPS_CFLAGS@

TEST_PROGS +=     \
   test_replace   \
   test_url       \
   test_strings

#check_PROGRAMS = $(TESTS)

# Let the .c source code know about these paths,
# even when sourcedir!=builddir, such as during make distcheck:
#
# We could set GRL_PLUGIN_PATH here in TESTS_ENVIRONMENT, specifying all the paths (separated by :) to all the plugins,
# but it is cleaner to restrict the test to just one plugin by doing it in the code.
# Note that the tmdb plugin Makefile.am copies the .xml file into .libs so we can use the plugin.

test_xml_factory_defines =                                                       \
   -DXML_FACTORY_PLUGIN_PATH=\""$(abs_top_builddir)/src/.libs/"\"    \
   -DXML_FACTORY_SPECS_PATH=\""$(abs_top_srcdir)/tests/sources/"\"   \
   -DXML_FACTORY_DATA_PATH=\""$(abs_top_srcdir)/tests/data/"\"

test_relace_SOURCES =	\
	test_replace.c

test_replace_LDADD =	\
	@DEPS_LIBS@

test_replace_CFLAGS =	\
	$(test_xml_factory_defines)

test_url_SOURCES =	\
	test_url.c

test_url_LDADD =	\
	@DEPS_LIBS@

test_url_CFLAGS =	\
	$(test_xml_factory_defines)

test_strings_SOURCES =	\
	test_strings.c

test_strings_LDADD =	\
	@DEPS_LIBS@

test_strings_CFLAGS =	\
	$(test_xml_factory_defines)

# Distribute the tests data:
dist_noinst_DATA =                     \
   data/network-data.ini               \
   data/test-url.data                  \
   data/test-url-album.data            \
   sources/xml-test-replace.xml        \
   sources/xml-test-url.xml            \
   sources/xml-test-empty-strings.xml  \
   sources/xml-test-strings.xml

noinst_PROGRAMS = $(TEST_PROGS)

MAINTAINERCLEANFILES =  \
   *.in                 \
   *~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)