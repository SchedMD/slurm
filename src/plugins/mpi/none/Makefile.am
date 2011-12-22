# Makefile for mpi/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER

pkglib_LTLIBRARIES = mpi_none.la
mpi_none_la_SOURCES = mpi_none.c
mpi_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_mpi_none_la_SOURCES = mpi_none.c
endif
