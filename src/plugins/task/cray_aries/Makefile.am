# Makefile for task/cray_aries plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(CRAY_TASK_CPPFLAGS)

pkglib_LTLIBRARIES = task_cray_aries.la

# Null task plugin.
task_cray_aries_la_SOURCES = task_cray_aries.c
task_cray_aries_la_LDFLAGS = $(PLUGIN_FLAGS) $(CRAY_TASK_LDFLAGS) \
	$(NUMA_LIBS)
