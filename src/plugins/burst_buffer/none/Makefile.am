# Makefile for burst_buffer/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(MUNGE_CPPFLAGS)

pkglib_LTLIBRARIES = burst_buffer_none.la
burst_buffer_none_la_SOURCES = burst_buffer_none.c
burst_buffer_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
