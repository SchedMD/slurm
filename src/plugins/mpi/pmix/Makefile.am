# Makefile for mpi/pmix plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(PMIX_CPPFLAGS)

pkglib_LTLIBRARIES = mpi_pmix.la 

# OpenMPI MPI plugin.
mpi_pmix_la_SOURCES = mpi_pmix.c pmixp_agent.c pmixp_client.c pmixp_coll.c pmixp_nspaces.c pmixp_info.c \
			pmixp_server.c pmixp_state.c pmixp_io.c pmixp_utils.c pmixp_dmdx.c \
			pmixp_agent.h pmixp_client.h pmixp_coll.h pmixp_nspaces.h pmixp_info.h \
			pmixp_server.h pmixp_state.h pmixp_io.h pmixp_utils.h pmixp_common.h pmixp_dmdx.h

mpi_pmix_la_LIBADD = \
	$(top_builddir)/src/slurmd/common/libslurmd_reverse_tree_math.la $(PMIX_LDFLAGS) $(PMIX_LIBS)

mpi_pmix_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)


