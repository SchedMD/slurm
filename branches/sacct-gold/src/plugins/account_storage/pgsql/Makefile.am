# Makefile for account_storage/pgsql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = account_storage_pgsql.la

# Pgsql storage plugin.
account_storage_pgsql_la_SOURCES = account_storage_pgsql.c
account_storage_pgsql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
account_storage_pgsql_la_CFLAGS = $(PGSQL_CFLAGS)
account_storage_pgsql_la_LIBADD = \
	$(top_builddir)/src/database/libslurm_pgsql.la $(PGSQL_LIBS)
account_storage_pgsql_la_DEPENDENCIES = \
	$(top_builddir)/src/database/libslurm_pgsql.la

