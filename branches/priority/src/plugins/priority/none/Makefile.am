# Makefile for priority/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = priority_none.la

# Null priority logging plugin.
priority_none_la_SOURCES = priority_none.c
priority_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
