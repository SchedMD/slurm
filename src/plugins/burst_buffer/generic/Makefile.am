# Makefile for burst_buffer/generic plugin

AUTOMAKE_OPTIONS = foreign

EXTRA_DIST =				\
	bb_get_state.example		\
	bb_start_stage_in.example	\
	bb_start_stage_out.example	\
	bb_stop_stage_out.example

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = burst_buffer_generic.la
burst_buffer_generic_la_SOURCES = burst_buffer_generic.c
burst_buffer_generic_la_LDFLAGS = $(PLUGIN_FLAGS)
burst_buffer_generic_la_LIBADD = ../common/libburst_buffer_common.la

force:
$(burst_buffer_generic_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
