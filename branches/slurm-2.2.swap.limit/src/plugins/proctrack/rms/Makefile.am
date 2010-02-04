# Makefile for proctrack/aix plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if HAVE_ELAN
pkglib_LTLIBRARIES = proctrack_rms.la
else
pkglib_LTLIBRARIES =
endif

proctrack_rms_la_SOURCES = proctrack_rms.c
proctrack_rms_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
proctrack_rms_la_LIBADD  = -lrmscall
