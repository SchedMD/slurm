# Makefile for mpi/openmpi plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_openmpi.la

# OpenMPI MPI plugin.
mpi_openmpi_la_SOURCES = mpi_openmpi.c
mpi_openmpi_la_LDFLAGS = $(PLUGIN_FLAGS)
