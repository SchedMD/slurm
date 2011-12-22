# Makefile for jobcomp/filetxt plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

JOBCOMP_SCRIPT_LA_SOURCES = jobcomp_script.c

if ! USE_LOADLEVELER

# Text file job completion logging plugin.
pkglib_LTLIBRARIES = jobcomp_script.la
jobcomp_script_la_SOURCES = $(JOBCOMP_SCRIPT_LA_SOURCES)
jobcomp_script_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_jobcomp_script_la_SOURCES = $(JOBCOMP_SCRIPT_LA_SOURCES)
endif
