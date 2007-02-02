# Makefile for mpi/gmpi plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_mpichgm.la

# Null switch plugin.
mpi_mpichgm_la_SOURCES = mpi_mpichgm.c mpichgm.c mpichgm.h\
			  $(top_srcdir)/src/common/mpi.h
mpi_mpichgm_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
