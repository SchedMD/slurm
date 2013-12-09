# Makefile for switch/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common \
	$(CRAY_CPPFLAGS) $(CRAY_SWITCH_CPPFLAGS)

AM_CFLAGS = $(CRAY_ALPSCOMM_CN_CFLAGS) $(CRAY_ALPSCOMM_SN_CFLAGS)

pkglib_LTLIBRARIES = switch_cray.la

# Null switch plugin.
switch_cray_la_SOURCES = switch_cray.c
switch_cray_la_CPPFLAGS = $(CRAY_SWITCH_CPPFLAGS) $(AM_CPPFLAGS)
switch_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(CRAY_LDFLAGS) $(CRAY_SWITCH_LDFLAGS)
switch_cray_la_LIBADD = $(CRAY_LDFLAGS)
