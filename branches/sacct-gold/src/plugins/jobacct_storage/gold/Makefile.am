# Makefile for jobacct_storage/gold plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

# Gold job completion logging plugin.
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_storage_gold.la
jobacct_storage_gold_la_SOURCES = jobacct_storage_gold.c
jobacct_storage_gold_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobacct_storage_gold_la_LIBADD  = \
	$(top_builddir)/src/database/libslurm_gold.la
jobacct_storage_gold_la_DEPENDENCIES = \
	$(top_builddir)/src/database/libslurm_gold.la


