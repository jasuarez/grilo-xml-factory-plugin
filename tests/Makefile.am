#
# Makefile.am
#
# Author: Iago Toral <itoral@igalia.com>
#
# Copyright (C) 2013 Igalia S.L.

include $(top_srcdir)/gtester.mk

INCLUDES = @DEPS_CFLAGS@

TEST_PROGS +=                    \
   test_xml_factory_result       \
   test_xml_factory_replace      \
   test_xml_factory_url          \
   test_xml_factory_regexp       \
   test_xml_factory_strings      \
   test_xml_factory_log          \
   test_xml_factory_private_keys \
   test_xml_factory_script       \
   test_xml_factory_expandable_string

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

test_xml_factory_result_SOURCES =	\
	test_xml_factory_result.c

test_xml_factory_result_LDADD =	\
	@DEPS_LIBS@

test_xml_factory_result_CFLAGS =	\
	$(test_xml_factory_defines)

test_xml_factory_replace_SOURCES =	\
	test_xml_factory_replace.c

test_xml_factory_replace_LDADD =	\
	@DEPS_LIBS@

test_xml_factory_replace_CFLAGS =	\
	$(test_xml_factory_defines)

test_xml_factory_url_SOURCES =	\
	test_xml_factory_url.c

test_xml_factory_url_LDADD =	\
	@DEPS_LIBS@

test_xml_factory_url_CFLAGS =	\
	$(test_xml_factory_defines)

test_xml_factory_regexp_SOURCES = \
	test_xml_factory_regexp.c

test_xml_factory_regexp_LDADD =	\
	@DEPS_LIBS@

test_xml_factory_regexp_CFLAGS =	\
	$(test_xml_factory_defines)

test_xml_factory_strings_SOURCES =	\
	test_xml_factory_strings.c

test_xml_factory_strings_LDADD =	\
	@DEPS_LIBS@

test_xml_factory_strings_CFLAGS =	\
	$(test_xml_factory_defines)

test_xml_factory_log_SOURCES =	\
	test_xml_factory_log.c

test_xml_factory_log_LDADD =	\
	@DEPS_LIBS@

test_xml_factory_log_CFLAGS =		\
	$(test_xml_factory_defines)	\
   -DXML_FACTORY_LOG_DUMP_FILE=\""$(abs_top_srcdir)/tests/xml-test-log.dump"\"


test_xml_factory_private_keys_SOURCES =	\
	test_xml_factory_private_keys.c

test_xml_factory_private_keys_LDADD =	\
	@DEPS_LIBS@

test_xml_factory_private_keys_CFLAGS =	\
	$(test_xml_factory_defines)

test_xml_factory_expandable_string_LDADD =	\
	@DEPS_LIBS@

test_xml_factory_expandable_string_CFLAGS =	\
	$(test_xml_factory_defines)

test_xml_factory_script_LDADD =	\
	@DEPS_LIBS@

test_xml_factory_script_CFLAGS =	\
	$(test_xml_factory_defines)

# Distribute the tests data:
dist_noinst_DATA =                                 \
   data/network-data.ini                           \
   data/test-url.data                              \
   data/test-url-album.data                        \
   sources/xml-test-replace.xml                    \
   sources/xml-test-url.xml                        \
   sources/xml-test-empty-strings.xml              \
	sources/xml-test-private-keys.xml               \
   sources/xml-test-regexp-full.xml                \
	sources/xml-test-regexp-decode-input.xml        \
   sources/xml-test-regexp-no-expression.xml       \
   sources/xml-test-regexp-no-input.xml            \
   sources/xml-test-regexp-no-output.xml           \
   sources/xml-test-regexp-repeat-expression.xml   \
   sources/xml-test-strings.xml                    \
   sources/xml-test-log.xml.in                     \
   sources/xml-test-expandable-string.xml          \
	sources/xml-test-script-init-success.xml

noinst_PROGRAMS = $(TEST_PROGS)

MAINTAINERCLEANFILES =  \
   *.in                 \
   *~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
