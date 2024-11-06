# Makefile for mpi/cray_shasta plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_cray_shasta.la

# Cray Shasta MPI plugin
mpi_cray_shasta_la_SOURCES = apinfo.c apinfo.h mpi_cray_shasta.c
mpi_cray_shasta_la_CFLAGS = $(AM_CFLAGS)
mpi_cray_shasta_la_LDFLAGS = $(PLUGIN_FLAGS)
