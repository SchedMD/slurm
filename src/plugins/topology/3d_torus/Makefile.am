# Makefile for topology/3d_torus plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = topology_3d_torus.la

# 3d_torus topology plugin.
topology_3d_torus_la_SOURCES =	\
	hilbert.c		\
	hilbert.h		\
	hilbert_slurm.c		\
	topology_3d_torus.c
topology_3d_torus_la_LDFLAGS = $(PLUGIN_FLAGS)
topology_3d_torus_la_LIBADD = ../common/libtopology_common.la

force:
$(topology_3d_torus_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
