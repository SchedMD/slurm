# Makefile for accounting_storage/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = accounting_storage_none.la

accounting_storage_none_la_SOURCES = accounting_storage_none.c
accounting_storage_none_la_LDFLAGS = $(PLUGIN_FLAGS)
