# Makefile for burst_buffer/generic plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(MUNGE_CPPFLAGS)

pkglib_LTLIBRARIES = burst_buffer_generic.la
burst_buffer_generic_la_SOURCES = burst_buffer_generic.c
burst_buffer_generic_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
