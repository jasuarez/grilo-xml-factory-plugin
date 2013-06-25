#
# Makefile.am
#
# Author: Juan A. Suarez Romero <jasuarez@igalia.com>
#
# Copyright (C) 2013 Igalia S.L. All rights reserved.

xml_sources_DATA =            \
   grl-lastfm-albumart.xml    \
   grl-tmdb.xml               \
   grl-aljazeera.xml          \
   grl-bbc-news.xml           \
   grl-digitally-imported.xml \
   grl-flickr-xml.xml         \
   grl-google-images.xml      \
   grl-jamendo.xml            \
   grl-justin-tv.xml          \
   grl-nascar.xml             \
   grl-online-radio.xml       \
   grl-live-streams.xml

xml_sourcesdir = $(datadir)/$(XML_FACTORY_SOURCE_LOCATION)

EXTRA_DIST = $(xml_sources_DATA)

MAINTAINERCLEANFILES =  \
   *.in                 \
   *~

DISTCLEANFILES = $(MAINTAINERCLEANFILES)
