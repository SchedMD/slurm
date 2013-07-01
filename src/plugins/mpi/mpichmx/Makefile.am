# Makefile for mpi/mx plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_mpichmx.la

# Null switch plugin.
mpi_mpichmx_la_SOURCES = mpi_mpichmx.c mpichmx.c mpichmx.h\
			  $(top_srcdir)/src/common/mpi.h
mpi_mpichmx_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
