# Makefile for topology/none plugin

AUTOMAKE_OPTIONS = foreign

TOPOLOGY_NONE_LA_SOURCES = topology_none.c

if ! USE_LOADLEVELER

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = topology_none.la

# Null topology plugin.
topology_none_la_SOURCES = $(TOPOLOGY_NONE_LA_SOURCES)
topology_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_topology_none_la_SOURCES = $(TOPOLOGY_NONE_LA_SOURCES)
endif
