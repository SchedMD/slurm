# Makefile for gres/tpu plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = gres_tpu.la

# TPU generic resource plugin.
gres_tpu_la_SOURCES = gres_tpu.c
gres_tpu_la_LDFLAGS = $(PLUGIN_FLAGS)
gres_tpu_la_LIBADD = ../common/libgres_common.la

force:
$(gres_tpu_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
