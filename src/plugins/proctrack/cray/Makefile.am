# Makefile for proctrack/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = - -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(CRAY_JOB_CPPFLAGS)

if HAVE_REAL_CRAY
PROC_PLUG = proctrack_cray.la
endif
pkglib_LTLIBRARIES = $(PROC_PLUG)

proctrack_cray_la_SOURCES = proctrack_cray.c
proctrack_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(CRAY_JOB_LDFLAGS)
