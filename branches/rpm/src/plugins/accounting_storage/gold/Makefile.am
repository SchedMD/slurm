# Makefile for accounting_storage/gold plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = accounting_storage_gold.la
accounting_storage_gold_la_SOURCES = accounting_storage_gold.c
accounting_storage_gold_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
accounting_storage_gold_la_LIBADD  = \
	$(top_builddir)/src/database/libslurm_gold.la
accounting_storage_gold_la_DEPENDENCIES = \
	$(top_builddir)/src/database/libslurm_gold.la


