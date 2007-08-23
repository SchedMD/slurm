# Makefile for storage/mysql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic $(MYSQL_LIBS)

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = storage_mysql.la

# Mysql storage plugin.
storage_mysql_la_SOURCES = storage_mysql.c \
		mysql_common.c mysql_common.h \
		mysql_jobacct.c mysql_jobacct.h \
		mysql_jobcomp.c mysql_jobcomp.h \
		mysql_jobacct_process.c mysql_jobacct_process.h \
		mysql_jobcomp_process.c mysql_jobcomp_process.h
storage_mysql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
storage_mysql_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

