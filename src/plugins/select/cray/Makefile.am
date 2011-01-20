# Makefile for select/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = select_cray.la

# Cray node selection plugin.
select_cray_la_SOURCES = select_cray.c other_select.c other_select.h
select_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

