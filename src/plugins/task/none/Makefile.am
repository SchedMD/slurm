# Makefile for task/none plugin

AUTOMAKE_OPTIONS = foreign

TASK_NONE_LA_SOURCES = task_none.c

if ! USE_LOADLEVELER

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = task_none.la

# Null task plugin.
task_none_la_SOURCES = $(TASK_NONE_LA_SOURCES)
task_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
pkglib_LTLIBRARIES =
EXTRA_task_none_la_SOURCES = $(TASK_NONE_LA_SOURCES)
endif
