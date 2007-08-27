# Makefile for jobacct-storage/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_storage_none.la

# Null job completion logging plugin.
jobacct_storage_none_la_SOURCES = jobacct_storage_none.c
jobacct_storage_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
