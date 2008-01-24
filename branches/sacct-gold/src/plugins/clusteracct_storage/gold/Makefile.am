# Makefile for clusteracct_storage/gold plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = clusteracct_storage_gold.la

# Null job completion logging plugin.
clusteracct_storage_gold_la_SOURCES = clusteracct_storage_gold.c
clusteracct_storage_gold_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
clusteracct_storage_gold_la_LIBADD = \
	$(top_builddir)/src/database/libslurm_gold.la
clusteracct_storage_gold_la_DEPENDENCIES = \
	$(top_builddir)/src/database/libslurm_gold.la
