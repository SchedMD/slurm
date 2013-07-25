# Makefile for task/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(CRAY_CPPFLAGS)

pkglib_LTLIBRARIES = task_cray.la

# Null task plugin.
task_cray_la_SOURCES = task_cray.c
task_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
task_cray_la_LIBADD = $(CRAY_LDFLAGS)
