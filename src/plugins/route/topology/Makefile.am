# Makefile for route/topology plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = route_topology.la

# topology route plugin.
route_topology_la_SOURCES = route_topology.c
route_topology_la_LDFLAGS = $(PLUGIN_FLAGS)
