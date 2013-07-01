# Makefile for topology/3d_torus plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = topology_3d_torus.la

# 3d_torus topology plugin.
topology_3d_torus_la_SOURCES =	\
	hilbert.c		\
	hilbert.h		\
	hilbert_slurm.c		\
	topology_3d_torus.c
topology_3d_torus_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
