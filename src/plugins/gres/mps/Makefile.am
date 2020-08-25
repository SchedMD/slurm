# Makefile for gres/mps plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(NVML_CPPFLAGS)

pkglib_LTLIBRARIES = gres_mps.la

# Gres MPS plugin.
gres_mps_la_SOURCES = gres_mps.c
gres_mps_la_LDFLAGS = $(PLUGIN_FLAGS)
gres_mps_la_LIBADD = ../common/libgres_common.la

force:
$(gres_mps_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
