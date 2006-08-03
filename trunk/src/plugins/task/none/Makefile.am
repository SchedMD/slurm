# Makefile for task/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = task_none.la

# Null task plugin.
task_none_la_SOURCES = task_none.c
task_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

if HAVE_AIX
task_none_la_LIBADD = $(top_builddir)/src/common/libcommon.la
endif
