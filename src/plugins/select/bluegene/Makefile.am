# $Id$
# Makefile for select/bluegene plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic  -lm

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = select_bluegene.la

# Blue Gene node selection plugin.
select_bluegene_la_SOURCES = select_bluegene.c \
				bluegene.c bluegene.h \
				partition_sys.c partition_sys.h \
				rm_api.h
select_bluegene_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) 

if HAVE_AIX
  select_bluegene_la_LIBADD  = $(top_builddir)/src/common/libcommon.la
endif
