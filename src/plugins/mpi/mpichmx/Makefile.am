# Makefile for mpi/mx plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

MPI_MPICHMX_LA_SOURCES =	\
	mpi_mpichmx.c		\
	mpichmx.c mpichmx.h	\
	$(top_srcdir)/src/common/mpi.h

if ! USE_LOADLEVELER

pkglib_LTLIBRARIES = mpi_mpichmx.la
mpi_mpichmx_la_SOURCES = $(MPI_MPICHMX_LA_SOURCES)
mpi_mpichmx_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_mpi_mpichmx_la_SOURCES = $(MPI_MPICHMX_LA_SOURCES)
endif
