# Makefile for mpi/pmix plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(HWLOC_CPPFLAGS) \
	$(UCX_CPPFLAGS)

pmix_src = mpi_pmix.c \
	pmixp_common.h pmixp_coll.h \
	pmixp_agent.c pmixp_client.c pmixp_nspaces.c pmixp_info.c \
	pmixp_agent.h pmixp_client.h pmixp_nspaces.h pmixp_info.h \
	pmixp_server.c pmixp_state.c pmixp_io.c pmixp_utils.c pmixp_dmdx.c \
	pmixp_server.h pmixp_state.h pmixp_io.h pmixp_utils.h pmixp_dmdx.h \
	pmixp_conn.c pmixp_dconn.c pmixp_dconn_tcp.c \
	pmixp_conn.h pmixp_dconn.h pmixp_dconn_tcp.h \
	pmixp_coll.c pmixp_coll_tree.c pmixp_coll_ring.c

if HAVE_UCX
pmix_src += pmixp_dconn_ucx.c pmixp_dconn_ucx.h
endif

pmix_internal_libs = \
	$(top_builddir)/src/slurmd/common/libslurmd_reverse_tree_math.la

pmix_ldflags = $(PLUGIN_FLAGS) $(HWLOC_LDFLAGS) $(UCX_LDFLAGS)
pmix_libadd = $(pmix_internal_libs) $(HWLOC_LIBS) $(UCX_LIBS)

# pkglib_v#dir is needed in each one of the below because of the way we
# use pkglib_v#_LTLIBRARIES even though it is the same for each one it still
# needs to be defined.

if HAVE_PMIX_V1

pkglib_v1dir=$(pkglibdir)
pkglib_v1_LTLIBRARIES = mpi_pmix_v1.la
mpi_pmix_v1_la_SOURCES = $(pmix_src) pmixp_client_v1.c
mpi_pmix_v1_la_LIBADD = $(pmix_libadd)
mpi_pmix_v1_la_LDFLAGS = $(pmix_ldflags) $(PMIX_V1_LDFLAGS)
mpi_pmix_v1_la_CPPFLAGS = $(AM_CPPFLAGS) $(PMIX_V1_CPPFLAGS) -DHAVE_PMIX_VER=1

endif

if HAVE_PMIX_V2

pkglib_v2dir=$(pkglibdir)
pkglib_v2_LTLIBRARIES = mpi_pmix_v2.la
mpi_pmix_v2_la_SOURCES = $(pmix_src) pmixp_client_v2.c
mpi_pmix_v2_la_LIBADD = $(pmix_libadd)
mpi_pmix_v2_la_LDFLAGS = $(pmix_ldflags) $(PMIX_V2_LDFLAGS)
mpi_pmix_v2_la_CPPFLAGS = $(AM_CPPFLAGS) $(PMIX_V2_CPPFLAGS) -DHAVE_PMIX_VER=2

endif

if HAVE_PMIX_V3

pkglib_v3dir=$(pkglibdir)
pkglib_v3_LTLIBRARIES = mpi_pmix_v3.la
mpi_pmix_v3_la_SOURCES = $(pmix_src) pmixp_client_v2.c
mpi_pmix_v3_la_LIBADD = $(pmix_libadd)
mpi_pmix_v3_la_LDFLAGS = $(pmix_ldflags) $(PMIX_V3_LDFLAGS)
mpi_pmix_v3_la_CPPFLAGS = $(AM_CPPFLAGS) $(PMIX_V3_CPPFLAGS) -DHAVE_PMIX_VER=3

endif

if HAVE_PMIX_V3
mpi_pmix_so := mpi_pmix_v3.so
else

if HAVE_PMIX_V2
mpi_pmix_so := mpi_pmix_v2.so
else

if HAVE_PMIX_V1
mpi_pmix_so := mpi_pmix_v1.so
endif

endif
endif

force:
$(pmix_internal_libs): force
	@cd `dirname $@` && $(MAKE) `basename $@`

install-exec-hook:
	rm -rf $(DESTDIR)$(pkglibdir)/mpi_pmix.so
	$(LN_S) ./$(mpi_pmix_so)  $(DESTDIR)$(pkglibdir)/mpi_pmix.so

uninstall-hook:
	rm -rf $(DESTDIR)$(pkglibdir)/mpi_pmix.so
