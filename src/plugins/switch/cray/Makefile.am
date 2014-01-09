# Makefile for switch/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common \
	$(CRAY_SWITCH_CPPFLAGS)

pkglib_LTLIBRARIES = switch_cray.la

# Null switch plugin.
switch_cray_la_SOURCES = switch_cray.c
switch_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(CRAY_SWITCH_LDFLAGS)
