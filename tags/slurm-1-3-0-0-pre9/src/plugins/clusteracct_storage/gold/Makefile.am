# Makefile for clusteracct_storage/gold plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = clusteracct_storage_gold.la

# Null job completion logging plugin.
clusteracct_storage_gold_la_SOURCES = clusteracct_storage_gold.c \
			gold_interface.c gold_interface.h \
			base64.c base64.h
clusteracct_storage_gold_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
