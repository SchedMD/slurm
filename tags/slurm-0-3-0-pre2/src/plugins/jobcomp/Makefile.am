# $Id$
# Makefile for jobcomp plugins

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobcomp_none.la jobcomp_filetxt.la

# Null job completion logging plugin.
jobcomp_none_la_SOURCES = jobcomp_none.c
jobcomp_none_la_LDFLAGS = $(PLUGIN_FLAGS)

# Text file job completion logging plugin.
jobcomp_filetxt_la_SOURCES = jobcomp_filetxt.c
jobcomp_filetxt_la_LDFLAGS = $(PLUGIN_FLAGS)
