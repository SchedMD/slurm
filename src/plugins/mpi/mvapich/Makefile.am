# Makefile for mpi/mvapich plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_mvapich.la

mpi_mvapich_la_SOURCES = mpi_mvapich.c mvapich.c mvapich.h\
	$(top_srcdir)/src/common/mpi.h

mpi_mvapich_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
