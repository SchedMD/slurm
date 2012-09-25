# Makefile for energy_accounting/rapl plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = energy_accounting_rapl.la

# cpu/core energy accounting plugin.
energy_accounting_rapl_la_SOURCES = energy_accounting_rapl.c \
	energy_accounting_rapl.h

energy_accounting_rapl_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

