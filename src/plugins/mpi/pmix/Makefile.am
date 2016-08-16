# Makefile for mpi/pmix plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(PMIX_CPPFLAGS)


pmix_src = mpi_pmix.c pmixp_agent.c pmixp_client.c pmixp_coll.c pmixp_nspaces.c pmixp_info.c \
			pmixp_server.c pmixp_state.c pmixp_io.c pmixp_utils.c pmixp_dmdx.c \
			pmixp_agent.h pmixp_client.h pmixp_coll.h pmixp_nspaces.h pmixp_info.h \
			pmixp_server.h pmixp_state.h pmixp_io.h pmixp_utils.h pmixp_common.h pmixp_dmdx.h

pmix_internal_libs = $(top_builddir)/src/slurmd/common/libslurmd_reverse_tree_math.la

pmix_ldflags = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(PMIX_LDFLAGS)

if HAVE_PMIX_V1

pkglib_LTLIBRARIES = mpi_pmix_v1.la
mpi_pmix_v1_la_SOURCES = $(pmix_src)
mpi_pmix_v1_la_LIBADD = $(pmix_internal_libs) $(PMIX_LIBS)
mpi_pmix_v1_la_LDFLAGS = $(pmix_ldflags)

endif

if HAVE_PMIX_V2

pkglib_LTLIBRARIES = mpi_pmix_v2.la
mpi_pmix_v2_la_SOURCES = $(pmix_src)
mpi_pmix_v2_la_LIBADD = $(pmix_internal_libs) $(PMIX_LIBS)
mpi_pmix_v2_la_LDFLAGS = $(pmix_ldflags)

endif

force:
$(pmix_internal_libs): force
	@cd `dirname $@` && $(MAKE) `basename $@`
