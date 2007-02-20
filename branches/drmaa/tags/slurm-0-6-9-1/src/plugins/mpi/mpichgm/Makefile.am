# $Id: Makefile.am,v 1.4 2004/07/26 23:43:53 jette Exp $
# Makefile for mpi/gmpi plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mpi_mpichgm.la

# Null switch plugin.
mpi_mpichgm_la_SOURCES = mpi_mpichgm.c mpichgm.c \
                         $(top_srcdir)/src/common/global_srun.c \
			  $(top_srcdir)/src/common/net.c
mpi_mpichgm_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
