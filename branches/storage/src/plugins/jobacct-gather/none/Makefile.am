# Makefile for jobacct-gather/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct-gather_none.la

# Null job completion logging plugin.
jobacct-gather_none_la_SOURCES = jobacct-gather_none.c
jobacct-gather_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
