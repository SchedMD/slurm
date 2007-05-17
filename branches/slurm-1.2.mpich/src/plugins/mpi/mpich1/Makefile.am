# Makefile for mpi/mvapich plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_mpich1.la

mpi_mpich1_la_SOURCES = mpi_mpich1.c mpich1.c mpich1.h\
	$(top_srcdir)/src/common/mpi.h

mpi_mpich1_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
