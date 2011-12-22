# Makefile for switch/none plugin

AUTOMAKE_OPTIONS = foreign

SWITCH_NONE_LA_SOURCES = switch_none.c

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common


if ! USE_LOADLEVELER
pkglib_LTLIBRARIES = switch_none.la
switch_none_la_SOURCES = $(SWITCH_NONE_LA_SOURCES)
switch_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
pkglib_LTLIBRARIES =
EXTRA_switch_none_la_SOURCES = $(SWITCH_NONE_LA_SOURCES)
endif
