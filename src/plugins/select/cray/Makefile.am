# Makefile for select/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common \
	-I$(top_srcdir)/src/plugins/select/other $(CRAY_SELECT_CPPFLAGS)

pkglib_LTLIBRARIES = select_cray.la

# Cray node selection plugin.
select_cray_la_SOURCES = select_cray.c
select_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(CRAY_SELECT_LDFLAGS)
select_cray_la_LIBADD  = ../other/libother_select.la
