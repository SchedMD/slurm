# Makefile for burst_buffer/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

if WITH_JSON_PARSER

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(DATAWARP_CPPFLAGS) $(JSON_CPPFLAGS)

pkglib_LTLIBRARIES = burst_buffer_cray.la
burst_buffer_cray_la_SOURCES = burst_buffer_cray.c
burst_buffer_cray_la_LDFLAGS = $(PLUGIN_FLAGS) $(DATAWARP_LDFLAGS) $(JSON_LDFLAGS)
burst_buffer_cray_la_LIBADD = ../common/libburst_buffer_common.la

force:
$(burst_buffer_cray_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_burst_buffer_cray_la_SOURCES = burst_buffer_cray.c
endif
