# Makefile for mpich1/p4 model plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_mpich1_p4.la

# Null MPI plugin.
mpi_mpich1_p4_la_SOURCES = mpich1_p4.c
mpi_mpich1_p4_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
