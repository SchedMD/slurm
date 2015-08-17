# Makefile for mpi/mvapich plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(MUNGE_CPPFLAGS)

if WITH_MUNGE
MVAPICH = mpi_mvapich.la
endif

pkglib_LTLIBRARIES = $(MVAPICH)

mpi_mvapich_la_SOURCES = mpi_mvapich.c mvapich.c mvapich.h \
	$(top_srcdir)/src/common/mpi.h

mpi_mvapich_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(MUNGE_LDFLAGS) $(MUNGE_LIBS)
