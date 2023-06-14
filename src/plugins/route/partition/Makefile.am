# Makefile for route/partition plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = route_partition.la

# Null route plugin.
route_partition_la_SOURCES = route_partition.c
route_partition_la_LDFLAGS = $(PLUGIN_FLAGS)
