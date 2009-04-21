# Makefile for accounting_storage/mysql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if WITH_MYSQL
pkglib_LTLIBRARIES = accounting_storage_mysql.la

# Mysql storage plugin.
accounting_storage_mysql_la_SOURCES = accounting_storage_mysql.c \
		mysql_jobacct_process.c mysql_jobacct_process.h \
		mysql_rollup.c mysql_rollup.h
accounting_storage_mysql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
accounting_storage_mysql_la_CFLAGS = $(MYSQL_CFLAGS)
accounting_storage_mysql_la_LIBADD = \
	$(top_builddir)/src/database/libslurm_mysql.la $(MYSQL_LIBS)

force:
$(accounting_storage_mysql_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
else
EXTRA_accounting_storage_mysql_la_SOURCES = accounting_storage_mysql.c \
		mysql_jobacct_process.c mysql_jobacct_process.h \
		mysql_rollup.c mysql_rollup.h
endif
