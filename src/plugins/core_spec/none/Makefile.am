# Makefile for core_spec/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = core_spec_none.la

# Null core specialization plugin.
core_spec_none_la_SOURCES = core_spec_none.c
core_spec_none_la_LDFLAGS = $(PLUGIN_FLAGS)
