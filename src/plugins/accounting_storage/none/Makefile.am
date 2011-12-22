# Makefile for accounting_storage/none plugin

AUTOMAKE_OPTIONS = foreign

ACCOUNTING_STORAGE_NONE_LA_SOURCES = accounting_storage_none.c

if ! USE_LOADLEVELER

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = accounting_storage_none.la

accounting_storage_none_la_SOURCES = $(ACCOUNTING_STORAGE_NONE_LA_SOURCES)
accounting_storage_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_accounting_storage_none_la_SOURCES = $(ACCOUNTING_STORAGE_NONE_LA_SOURCES)
endif
