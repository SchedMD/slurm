# Makefile for nodeacct_storage/gold plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = nodeacct_storage_gold.la

# Null job completion logging plugin.
nodeacct_storage_gold_la_SOURCES = nodeacct_storage_gold.c
nodeacct_storage_gold_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
