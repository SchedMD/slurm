# Makefile for route/default plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = route_default.la

# Null route plugin.
route_default_la_SOURCES = route_default.c
route_default_la_LDFLAGS = $(PLUGIN_FLAGS)
