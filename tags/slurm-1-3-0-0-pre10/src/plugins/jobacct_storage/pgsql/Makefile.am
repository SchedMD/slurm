# Makefile for jobacct_storage/pgsql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_storage_pgsql.la

# Pgsql storage plugin.
jobacct_storage_pgsql_la_SOURCES = jobacct_storage_pgsql.c \
		pgsql_jobacct_process.c pgsql_jobacct_process.h 
jobacct_storage_pgsql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobacct_storage_pgsql_la_CFLAGS = $(PGSQL_CFLAGS)
jobacct_storage_pgsql_la_LIBADD = \
	$(top_builddir)/src/database/libslurm_pgsql.la $(PGSQL_LIBS)
jobacct_storage_pgsql_la_DEPENDENCIES = \
	$(top_builddir)/src/database/libslurm_pgsql.la

