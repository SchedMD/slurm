# Makefile for jobcomp/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

JOBCOMP_NONE_LA_SOURCES = jobcomp_none.c

if ! USE_LOADLEVELER

# Null job completion logging plugin.
pkglib_LTLIBRARIES = jobcomp_none.la
jobcomp_none_la_SOURCES = $(JOBCOMP_NONE_LA_SOURCES)
jobcomp_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
pkglib_LTLIBRARIES =
EXTRA_jobcomp_none_la_SOURCES = $(JOBCOMP_NONE_LA_SOURCES)
endif
