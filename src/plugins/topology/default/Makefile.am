# Makefile for topology/default plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = topology_default.la

# Null topology plugin.
topology_default_la_SOURCES = topology_default.c
topology_default_la_LDFLAGS = $(PLUGIN_FLAGS)
topology_default_la_LIBADD = ../common/libtopology_common.la

force:
$(topology_default_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
