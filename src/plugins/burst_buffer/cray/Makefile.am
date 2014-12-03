# Makefile for burst_buffer/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(MUNGE_CPPFLAGS)

pkglib_LTLIBRARIES = burst_buffer_cray.la
burst_buffer_cray_la_SOURCES = burst_buffer_cray.c
burst_buffer_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
burst_buffer_cray_la_LIBADD = ../common/libburst_buffer_common.la

force:
$(burst_buffer_cray_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
