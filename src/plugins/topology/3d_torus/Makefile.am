# Makefile for topology/3d_torus plugin

AUTOMAKE_OPTIONS = foreign

TOPOLOGY_3D_TORUS_LA_SOURCES =	\
	hilbert.c		\
	hilbert.h		\
	hilbert_slurm.c		\
	topology_3d_torus.c

if ! USE_LOADLEVELER

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = topology_3d_torus.la

# 3d_torus topology plugin.
topology_3d_torus_la_SOURCES =$(TOPOLOGY_3D_TORUS_LA_SOURCES)

topology_3d_torus_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_topology_3d_torus_la_SOURCES =$(TOPOLOGY_3D_TORUS_LA_SOURCES)
endif
