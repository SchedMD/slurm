# Makefile for select/bgq plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = select_bgq.la

# Linear node selection plugin.
select_bgq_la_SOURCES = select_bgq.cc bgq.h
select_bgq_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

