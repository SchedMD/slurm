# Makefile for topology/node_rank plugin

AUTOMAKE_OPTIONS = foreign

TOPOLOGY_NODE_RANK_LA_SOURCES = topology_node_rank.c

if ! USE_LOADLEVELER

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = topology_node_rank.la

# Null topology plugin.
topology_node_rank_la_SOURCES = $(TOPOLOGY_NODE_RANK_LA_SOURCES)
topology_node_rank_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_topology_node_rank_la_SOURCES = $(TOPOLOGY_NODE_RANK_LA_SOURCES)
endif
