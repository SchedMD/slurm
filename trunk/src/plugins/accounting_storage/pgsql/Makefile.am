# Makefile for accounting_storage/pgsql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

AS_PG_SOURCES= accounting_storage_pgsql.c accounting_storage_pgsql.h \
	       as_pg_common.c as_pg_common.h \
	       as_pg_acct.c as_pg_acct.h \
	       as_pg_archive.c as_pg_archive.h \
	       as_pg_assoc.c as_pg_assoc.h \
	       as_pg_cluster.c as_pg_cluster.h \
	       as_pg_event.c as_pg_event.h \
	       as_pg_job.c as_pg_job.h \
	       as_pg_problem.c as_pg_problem.h \
	       as_pg_qos.c as_pg_qos.h \
	       as_pg_resv.c as_pg_resv.h \
	       as_pg_rollup.c as_pg_rollup.h \
	       as_pg_txn.c as_pg_txn.h \
	       as_pg_usage.c as_pg_usage.h \
	       as_pg_user.c as_pg_user.h \
	       as_pg_wckey.c as_pg_wckey.h

if WITH_PGSQL
pkglib_LTLIBRARIES = accounting_storage_pgsql.la

# Pgsql storage plugin.
accounting_storage_pgsql_la_SOURCES = $(AS_PG_SOURCES)
accounting_storage_pgsql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
accounting_storage_pgsql_la_CFLAGS = $(PGSQL_CFLAGS)
accounting_storage_pgsql_la_LIBADD = \
	$(top_builddir)/src/database/libslurm_pgsql.la $(PGSQL_LIBS) \
	../common/libaccounting_storage_common.la

force:
$(accounting_storage_pgsql_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
else
EXTRA_accounting_storage_pgsql_la_SOURCES = $(AS_PG_SOURCES)
endif
