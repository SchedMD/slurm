# Makefile for jobcomp/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobcomp_none.la

# Null job completion logging plugin.
jobcomp_none_la_SOURCES = jobcomp_none.c
jobcomp_none_la_LDFLAGS = $(PLUGIN_FLAGS)
