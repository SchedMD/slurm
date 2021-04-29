# Makefile for node_features_helpers plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = node_features_helpers.la
node_features_helpers_la_SOURCES = node_features_helpers.c
node_features_helpers_la_LDFLAGS = $(PLUGIN_FLAGS)

force:
$(node_features_helpers_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
