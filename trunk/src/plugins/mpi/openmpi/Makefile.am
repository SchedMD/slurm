# Makefile for mpi/openmpi plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_openmpi.la

# Null MPI plugin.
mpi_none_la_SOURCES = mpi_openmpi.c
mpi_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
