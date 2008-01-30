# Makefile for account_storage/mysql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = account_storage_mysql.la

# Mysql storage plugin.
account_storage_mysql_la_SOURCES = account_storage_mysql.c
account_storage_mysql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
account_storage_mysql_la_CFLAGS = $(MYSQL_CFLAGS)
account_storage_mysql_la_LIBADD = \
	$(top_builddir)/src/database/libslurm_mysql.la $(MYSQL_LIBS)
account_storage_mysql_la_DEPENDENCIES = \
	$(top_builddir)/src/database/libslurm_mysql.la

