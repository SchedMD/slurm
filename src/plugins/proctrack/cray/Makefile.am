# Makefile for proctrack/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = - -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(CRAY_JOB_CPPFLAGS)

pkglib_LTLIBRARIES = proctrack_cray.la

proctrack_cray_la_SOURCES = proctrack_cray.c
proctrack_cray_la_LDFLAGS = $(PLUGIN_FLAGS) $(CRAY_JOB_LDFLAGS)
