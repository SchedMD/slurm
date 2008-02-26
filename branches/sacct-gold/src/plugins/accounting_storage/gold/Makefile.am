# Makefile for account_storage/gold plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = account_storage_gold.la
account_storage_gold_la_SOURCES = account_storage_gold.c
account_storage_gold_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
account_storage_gold_la_LIBADD  = \
	$(top_builddir)/src/database/libslurm_gold.la
account_storage_gold_la_DEPENDENCIES = \
	$(top_builddir)/src/database/libslurm_gold.la


