# Makefile for accounting_storage/slurmdbd plugin

AUTOMAKE_OPTIONS = foreign

ACCOUNTING_STORAGE_SLURMDBD_LA_SOURCES = accounting_storage_slurmdbd.c

if ! USE_LOADLEVELER

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = accounting_storage_slurmdbd.la

# Null job completion logging plugin.
accounting_storage_slurmdbd_la_SOURCES = $(ACCOUNTING_STORAGE_SLURMDBD_LA_SOURCES)
accounting_storage_slurmdbd_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_accounting_storage_slurmdbd_la_SOURCES = $(ACCOUNTING_STORAGE_SLURMDBD_LA_SOURCES)
endif
