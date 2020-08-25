# Makefile for power/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = power_none.la
power_none_la_SOURCES = power_none.c
power_none_la_LDFLAGS = $(PLUGIN_FLAGS)
power_none_la_LIBADD = ../common/libpower_common.la

force:
$(power_none_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
