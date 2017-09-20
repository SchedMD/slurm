# Makefile for mcs/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mcs_none.la

# mcs/none plugin.
mcs_none_la_SOURCES = mcs_none.c
mcs_none_la_LDFLAGS = $(PLUGIN_FLAGS)
