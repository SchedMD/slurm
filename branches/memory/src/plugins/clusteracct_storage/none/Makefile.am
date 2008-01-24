# Makefile for clusteracct_storage/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = clusteracct_storage_none.la

# Null job completion logging plugin.
clusteracct_storage_none_la_SOURCES = clusteracct_storage_none.c
clusteracct_storage_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
