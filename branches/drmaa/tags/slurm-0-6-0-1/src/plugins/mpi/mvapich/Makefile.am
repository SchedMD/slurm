# $Id: Makefile.am,v 1.4 2004/07/26 23:43:53 jette Exp $
# Makefile for mpi/mvapich plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_mvapich.la

# Null switch plugin.
mpi_mvapich_la_SOURCES = mpi_mvapich.c mvapich.c
mpi_mvapich_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
mpi_mvapich_la_LIBADD  =  \
	$(top_builddir)/src/common/libcommon.la -lpthread \
	$(top_builddir)/src/api/libslurm.la
