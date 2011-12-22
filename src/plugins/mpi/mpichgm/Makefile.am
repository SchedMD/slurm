# Makefile for mpi/gmpi plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

MPI_MPICHGM_LA_SOURCES =	\
	mpi_mpichgm.c		\
	mpichgm.c mpichgm.h	\
	$(top_srcdir)/src/common/mpi.h

if ! USE_LOADLEVELER

pkglib_LTLIBRARIES = mpi_mpichgm.la
mpi_mpichgm_la_SOURCES = $(MPI_MPICHGM_LA_SOURCES)
mpi_mpichgm_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_mpi_mpichgm_la_SOURCES = $(MPI_MPICHGM_LA_SOURCES)
endif
