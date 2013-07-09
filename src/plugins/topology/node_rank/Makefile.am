# Makefile for topology/node_rank plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = topology_node_rank.la

# Null topology plugin.
topology_node_rank_la_SOURCES = topology_node_rank.c
topology_node_rank_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
