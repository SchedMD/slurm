# Makefile for ext_sensors/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = ext_sensors_none.la

# Null job completion logging plugin.
ext_sensors_none_la_SOURCES = ext_sensors_none.c

ext_sensors_none_la_LDFLAGS = $(PLUGIN_FLAGS)
