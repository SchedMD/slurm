# Makefile for accounting_storage/pgsql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

AS_PG_SOURCES= accounting_storage_pgsql.c common.c common.h \
		account.c association.c cluster.c clusteracct.c  \
		jobacct.c problem.c qos.c reservation.c rollup.c \
		txn.c usage.c user.c wckey.c \
		$(top_srcdir)/src/plugins/accounting_storage/common/common_as.h \
		$(top_srcdir)/src/plugins/accounting_storage/common/common_as.c

if WITH_PGSQL
pkglib_LTLIBRARIES = accounting_storage_pgsql.la

# Pgsql storage plugin.
accounting_storage_pgsql_la_SOURCES = $(AS_PG_SOURCES)
accounting_storage_pgsql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
accounting_storage_pgsql_la_CFLAGS = $(PGSQL_CFLAGS)
accounting_storage_pgsql_la_LIBADD = \
	$(top_builddir)/src/database/libslurm_pgsql.la $(PGSQL_LIBS)

force:
$(accounting_storage_pgsql_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
else
EXTRA_accounting_storage_pgsql_la_SOURCES = $(AS_PG_SOURCES)
endif
