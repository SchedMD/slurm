# Makefile for jobcomp/filetxt plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobcomp_filetxt.la

# Text file job completion logging plugin.
jobcomp_filetxt_la_SOURCES = jobcomp_filetxt.c \
			filetxt_jobcomp_process.c filetxt_jobcomp_process.h

jobcomp_filetxt_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
