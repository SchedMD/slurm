# $Id$
# Makefile for proctrack/sid plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = proctrack_sid.la

# Process group ID process tracking  plugin.
proctrack_sid_la_SOURCES = proctrack_sid.c
proctrack_sid_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

if HAVE_AIX
proctrack_sid_la_LIBADD  = $(top_builddir)/src/common/libcommon.la
endif
