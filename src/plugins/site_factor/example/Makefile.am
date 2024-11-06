# Makefile for site_factor/example plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = site_factor_example.la

# site_factor NONE plugin.
site_factor_example_la_SOURCES = site_factor_example.c
site_factor_example_la_LDFLAGS = $(PLUGIN_FLAGS)

force:
$(site_factor_example_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
