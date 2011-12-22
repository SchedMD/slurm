# Makefile for mpi/openmpi plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER

pkglib_LTLIBRARIES = mpi_openmpi.la
mpi_openmpi_la_SOURCES = mpi_openmpi.c
mpi_openmpi_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_mpi_openmpi_la_SOURCES = mpi_openmpi.c
endif
