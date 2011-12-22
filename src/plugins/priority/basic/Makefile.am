# Makefile for priority/basic plugin

AUTOMAKE_OPTIONS = foreign

PRIORITY_BASIC_LA_SOURCES = priority_basic.c

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER

# basic priority logging plugin.
pkglib_LTLIBRARIES = priority_basic.la

priority_basic_la_SOURCES = $(PRIORITY_BASIC_LA_SOURCES)
priority_basic_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
pkglib_LTLIBRARIES =
EXTRA_priority_basic_la_SOURCES = $(PRIORITY_BASIC_LA_SOURCES)
endif
