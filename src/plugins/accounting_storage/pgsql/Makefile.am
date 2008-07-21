# Makefile for accounting_storage/pgsql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = accounting_storage_pgsql.la

# Pgsql storage plugin.
accounting_storage_pgsql_la_SOURCES = accounting_storage_pgsql.c \
		pgsql_jobacct_process.c pgsql_jobacct_process.h
accounting_storage_pgsql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
accounting_storage_pgsql_la_CFLAGS = $(PGSQL_CFLAGS)
accounting_storage_pgsql_la_LIBADD = \
	$(top_builddir)/src/database/libslurm_pgsql.la $(PGSQL_LIBS)

force:
$(accounting_storage_pgsql_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
