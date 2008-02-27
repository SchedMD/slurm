# Makefile for jobcomp/slurmdbd plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobcomp_slurmdbd.la

# Null job completion logging plugin.
jobcomp_slurmdbd_la_SOURCES = jobcomp_slurmdbd.c
jobcomp_slurmdbd_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
