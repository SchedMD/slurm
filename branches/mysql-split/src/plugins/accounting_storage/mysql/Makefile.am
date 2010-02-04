# Makefile for accounting_storage/mysql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

AS_MYSQL_SOURCES = accounting_storage_mysql.c \
		mysql_acct.c mysql_acct.h \
		mysql_assoc.c mysql_accoc.h \
		mysql_cluster.c mysql_cluster.h \
		mysql_jobacct_process.c mysql_jobacct_process.h \
		mysql_problems.c mysql_problems.h \
		mysql_qos.c mysql_qos.h \
		mysql_rollup.c mysql_rollup.h \
		mysql_txn.c mysql_txn.h \
		mysql_user.c mysql_user.h \
		mysql_wckey.c mysql_wckey.h \
		$(top_srcdir)/src/plugins/accounting_storage/common/common_as.h \
		$(top_srcdir)/src/plugins/accounting_storage/common/common_as.c

if WITH_MYSQL
pkglib_LTLIBRARIES = accounting_storage_mysql.la

# Mysql storage plugin.
accounting_storage_mysql_la_SOURCES = $(AS_MYSQL_SOURCES)
accounting_storage_mysql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
accounting_storage_mysql_la_CFLAGS = $(MYSQL_CFLAGS)
accounting_storage_mysql_la_LIBADD = \
	$(top_builddir)/src/database/libslurm_mysql.la $(MYSQL_LIBS)

force:
$(accounting_storage_mysql_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
else
EXTRA_accounting_storage_mysql_la_SOURCES = $(AS_MYSQL_SOURCES)
endif
