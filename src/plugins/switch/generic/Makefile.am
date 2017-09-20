# Makefile for switch/generic plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = switch_generic.la

# Null switch plugin.
switch_generic_la_SOURCES = switch_generic.c
switch_generic_la_LDFLAGS = $(PLUGIN_FLAGS)
