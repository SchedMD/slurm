# Makefile for runtime/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = runtime_none.la

# No-op runtime plugin.
runtime_none_la_SOURCES = runtime_none.c
runtime_none_la_LDFLAGS = $(PLUGIN_FLAGS)


force:
$(runtime_none_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
