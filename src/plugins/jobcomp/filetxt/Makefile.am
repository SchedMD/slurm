# Makefile for jobcomp/filetxt plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

JOBCOMP_FILETXT_LA_SOURCES =	\
	jobcomp_filetxt.c	\
	filetxt_jobcomp_process.c \
	filetxt_jobcomp_process.h

if ! USE_LOADLEVELER

# Text file job completion logging plugin.
pkglib_LTLIBRARIES = jobcomp_filetxt.la
jobcomp_filetxt_la_SOURCES = $(JOBCOMP_FILETXT_LA_SOURCES)
jobcomp_filetxt_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_jobcomp_filetxt_la_SOURCES = $(JOBCOMP_FILETXT_LA_SOURCES)
endif
