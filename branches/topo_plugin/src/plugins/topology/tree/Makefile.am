# Makefile for topology/tree plugin

CPPFLAGS =  -DTOPOLOGY_CONFIG_FILE=\"$(sysconfdir)/topology.conf\"
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = topology_tree.la

# Null topology plugin.
topology_tree_la_SOURCES = topology_tree.c
topology_tree_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
