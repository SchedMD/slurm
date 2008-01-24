# Makefile for clusteracct_storage/pgsql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = clusteracct_storage_pgsql.la

# Null job completion logging plugin.
clusteracct_storage_pgsql_la_SOURCES = clusteracct_storage_pgsql.c
clusteracct_storage_pgsql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
clusteracct_storage_pgsql_la_CFLAGS = $(PGSQL_CFLAGS)
clusteracct_storage_pgsql_la_LIBADD = \
	$(top_builddir)/src/database/libslurm_pgsql.la $(PGSQL_LIBS)
clusteracct_storage_pgsql_la_DEPENDENCIES = \
	$(top_builddir)/src/database/libslurm_pgsql.la
