# Makefile for burst_buffer/datawarp plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) $(JSON_CPPFLAGS)

pkglib_LTLIBRARIES = burst_buffer_datawarp.la
burst_buffer_datawarp_la_SOURCES = burst_buffer_datawarp.c
burst_buffer_datawarp_la_LDFLAGS = $(PLUGIN_FLAGS) $(JSON_LDFLAGS)
burst_buffer_datawarp_la_LIBADD = ../common/libburst_buffer_common.la

force:
$(burst_buffer_datawarp_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
