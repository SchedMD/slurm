# Makefile for switch/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = switch_none.la

# Null switch plugin.
switch_none_la_SOURCES = switch_none.c
switch_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

if HAVE_AIX
switch_none_la_LIBADD = $(top_builddir)/src/common/libcommon.la
endif
