# Makefile for cli_filter/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = cli_filter_none.la

# cli_filter none plugin.
cli_filter_none_la_SOURCES = cli_filter_none.c
cli_filter_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
