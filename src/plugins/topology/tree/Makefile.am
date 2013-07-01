# Makefile for topology/tree plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = topology_tree.la

# Null topology plugin.
topology_tree_la_SOURCES = topology_tree.c
topology_tree_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
