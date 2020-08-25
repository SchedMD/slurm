# Makefile for gres/gpu plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = gres_gpu.la

# Gres GPU plugin.
gres_gpu_la_SOURCES = gres_gpu.c
gres_gpu_la_LDFLAGS = $(PLUGIN_FLAGS)
gres_gpu_la_LIBADD = ../common/libgres_common.la

force:
$(gres_gpu_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
