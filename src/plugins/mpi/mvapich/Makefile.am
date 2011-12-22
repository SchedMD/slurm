# Makefile for mpi/mvapich plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

MPI_MVAPICH_LA_SOURCES =	\
	mpi_mvapich.c		\
	mvapich.c mvapich.h	\
	$(top_srcdir)/src/common/mpi.h

if ! USE_LOADLEVELER

pkglib_LTLIBRARIES = mpi_mvapich.la
mpi_mvapich_la_SOURCES = $(MPI_MVAPICH_LA_SOURCES)
mpi_mvapich_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_mpi_mvapich_la_SOURCES = $(MPI_MVAPICH_LA_SOURCES)
endif
