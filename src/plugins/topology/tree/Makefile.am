# Makefile for topology/tree plugin

AUTOMAKE_OPTIONS = foreign

TOPOLOGY_TREE_LA_SOURCES = topology_tree.c

CPPFLAGS =  -DTOPOLOGY_CONFIG_FILE=\"$(sysconfdir)/topology.conf\"

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER
# Null topology plugin.
pkglib_LTLIBRARIES = topology_tree.la
topology_tree_la_SOURCES = $(TOPOLOGY_TREE_LA_SOURCES)
topology_tree_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
pkglib_LTLIBRARIES =
EXTRA_topology_tree_la_SOURCES = $(TOPOLOGY_TREE_LA_SOURCES)
endif
