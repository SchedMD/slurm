# Makefile for task/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(CRAY_CPPFLAGS)

AM_CFLAGS = $(CRAY_ALPSCOMM_CN_CFLAGS) -lnuma

pkglib_LTLIBRARIES = task_cray.la

# Null task plugin.
task_cray_la_SOURCES = task_cray.c
task_cray_la_CPPFLAGS = $(CRAY_TASK_CPPFLAGS) $(AM_CPPFLAGS)
task_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(CRAY_LDFLAGS) $(CRAY_TASK_LDFLAGS)
task_cray_la_LIBADD = $(CRAY_LDFLAGS)
