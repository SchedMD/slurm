# Makefile for jobcomp/mysql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic $(MYSQL_LIBS)

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobcomp_mysql.la

# Mysql storage plugin.
jobcomp_mysql_la_SOURCES = jobcomp_mysql.c \
		mysql_jobcomp.c mysql_jobcomp.h \
		mysql_jobcomp_process.c mysql_jobcomp_process.h
jobcomp_mysql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobcomp_mysql_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

