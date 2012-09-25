# Makefile for energy_accounting/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = energy_accounting_none.la

# Null job completion logging plugin.
energy_accounting_none_la_SOURCES = energy_accounting_none.c

energy_accounting_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

