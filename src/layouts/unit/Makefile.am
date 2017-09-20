AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = layouts_unit_default.la

layouts_unit_default_la_SOURCES = default.c
layouts_unit_default_la_LDFLAGS = $(PLUGIN_FLAGS)
