# Makefile for jobacct_storage/mysql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_storage_mysql.la

# Mysql storage plugin.
jobacct_storage_mysql_la_SOURCES = jobacct_storage_mysql.c \
		mysql_jobacct_process.c mysql_jobacct_process.h 
jobacct_storage_mysql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobacct_storage_mysql_la_LIBS = $(MYSQL_LIBS)
jobacct_storage_mysql_la_CFLAGS = $(MYSQL_CFLAGS)
jobacct_storage_mysql_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

