# Makefile for topology/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = topology_none.la

# Null topology plugin.
topology_none_la_SOURCES = topology_none.c
topology_none_la_LDFLAGS = $(PLUGIN_FLAGS)
