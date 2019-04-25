# Makefile for core_spec/cray_aries plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = core_spec_cray_aries.la

# Cray/Aries core specialization plugin.
core_spec_cray_aries_la_SOURCES = core_spec_cray_aries.c
core_spec_cray_aries_la_CPPFLAGS = $(CRAY_JOB_CPPFLAGS) $(AM_CPPFLAGS)
core_spec_cray_aries_la_LDFLAGS = $(CRAY_JOB_LDFLAGS) $(PLUGIN_FLAGS)
