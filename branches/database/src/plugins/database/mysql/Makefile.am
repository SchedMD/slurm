# Makefile for database/mysql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic $(MYSQL_LIBS)

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = database_mysql.la

# Mysql database plugin.
database_mysql_la_SOURCES = database_mysql.c
database_mysql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
database_mysql_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

