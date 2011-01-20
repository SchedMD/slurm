# Makefile for accounting_storage/slurmdbd plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = accounting_storage_slurmdbd.la

# Null job completion logging plugin.
accounting_storage_slurmdbd_la_SOURCES = accounting_storage_slurmdbd.c
accounting_storage_slurmdbd_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)



