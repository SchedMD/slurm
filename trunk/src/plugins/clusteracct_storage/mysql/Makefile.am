# Makefile for nodeacct_storage/mysql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = nodeacct_storage_mysql.la

# Null job completion logging plugin.
nodeacct_storage_mysql_la_SOURCES = nodeacct_storage_mysql.c
nodeacct_storage_mysql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
