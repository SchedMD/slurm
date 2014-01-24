# Makefile for accounting_storage/mysql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

AS_MYSQL_SOURCES = accounting_storage_mysql.c accounting_storage_mysql.h \
		as_mysql_acct.c as_mysql_acct.h \
		as_mysql_archive.c as_mysql_archive.h \
		as_mysql_assoc.c as_mysql_assoc.h \
		as_mysql_cluster.c as_mysql_cluster.h \
		as_mysql_job.c as_mysql_job.h \
		as_mysql_jobacct_process.c as_mysql_jobacct_process.h \
		as_mysql_problems.c as_mysql_problems.h \
		as_mysql_qos.c as_mysql_qos.h \
		as_mysql_resource.c as_mysql_resource.h \
		as_mysql_resv.c as_mysql_resv.h \
		as_mysql_rollup.c as_mysql_rollup.h \
		as_mysql_txn.c as_mysql_txn.h \
		as_mysql_usage.c as_mysql_usage.h \
		as_mysql_user.c as_mysql_user.h \
		as_mysql_wckey.c as_mysql_wckey.h

if WITH_MYSQL
pkglib_LTLIBRARIES = accounting_storage_mysql.la

# Mysql storage plugin.
accounting_storage_mysql_la_SOURCES = $(AS_MYSQL_SOURCES)
accounting_storage_mysql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
accounting_storage_mysql_la_CFLAGS = $(MYSQL_CFLAGS)
accounting_storage_mysql_la_LIBADD = \
	$(top_builddir)/src/database/libslurm_mysql.la $(MYSQL_LIBS) \
	../common/libaccounting_storage_common.la

force:
$(accounting_storage_mysql_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
else
EXTRA_accounting_storage_mysql_la_SOURCES = $(AS_MYSQL_SOURCES)
endif
