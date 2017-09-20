# Makefile for mpi/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_none.la

# Null MPI plugin.
mpi_none_la_SOURCES = mpi_none.c
mpi_none_la_LDFLAGS = $(PLUGIN_FLAGS)
