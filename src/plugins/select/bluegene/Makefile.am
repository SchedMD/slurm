# $Id$
# Makefile for select/bluegene plugin

CPPFLAGS =  -DBLUEGENE_CONFIG_FILE=\"$(sysconfdir)/bluegene.conf\"
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic -lm

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(BGL_INCLUDES)

pkglib_LTLIBRARIES = select_bluegene.la

# Blue Gene node selection plugin.
select_bluegene_la_SOURCES = select_bluegene.c \
				bluegene.c bluegene.h \
				partition_sys.c partition_sys.h
select_bluegene_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(BGL_LDFLAGS)

if HAVE_AIX
  select_bluegene_la_LIBADD  = $(top_builddir)/src/common/libcommon.la
endif
