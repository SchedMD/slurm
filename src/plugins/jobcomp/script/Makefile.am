# Makefile for jobcomp/filetxt plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobcomp_script.la

# Text file job completion logging plugin.
jobcomp_script_la_SOURCES = jobcomp_script.c
jobcomp_script_la_LDFLAGS = $(PLUGIN_FLAGS)
