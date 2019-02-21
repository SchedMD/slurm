# Makefile for gpu/generic plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = gpu_generic.la

# GPU GENERIC plugin.
gpu_generic_la_SOURCES = gpu_generic.c
gpu_generic_la_LDFLAGS = $(PLUGIN_FLAGS)

force:
$(gpu_generic_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
