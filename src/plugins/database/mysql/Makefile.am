# Makefile for database/mysql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic $(MYSQL_LIBS)

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = database_mysql.la

# Mysql database plugin.
database_mysql_la_SOURCES = database_mysql.c \
		mysql_common.c mysql_common.h \
		mysql_jobacct.c mysql_jobacct.h \
		mysql_jobcomp.c mysql_jobcomp.h \
		mysql_jobacct_process.c mysql_jobacct_process.h \
		mysql_jobcomp_process.c mysql_jobcomp_process.h
database_mysql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
database_mysql_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

