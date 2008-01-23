# Makefile for nodeacct_storage/pgsql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = nodeacct_storage_pgsql.la

# Null job completion logging plugin.
nodeacct_storage_pgsql_la_SOURCES = nodeacct_storage_pgsql.c
nodeacct_storage_pgsql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
