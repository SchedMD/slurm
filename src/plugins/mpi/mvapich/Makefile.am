# Makefile for mpi/mvapich plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_mvapich.la

# Null switch plugin.
mpi_mvapich_la_SOURCES = mpi_mvapich.c mvapich.c mvapich.h\
	$(top_srcdir)/src/common/net.c \
	$(top_srcdir)/src/srun/srun_job.c

mpi_mvapich_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
