# Makefile for database/pgsql plugin

CPPFLAGS = $(PGSQL_CFLAGS)
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic $(PGSQL_LIBS)

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = database_pgsql.la

# Pgsql database plugin.
database_pgsql_la_SOURCES = database_pgsql.c \
		pgsql_common.c pgsql_common.h \
		pgsql_jobacct.c pgsql_jobacct.h \
		pgsql_jobcomp.c pgsql_jobcomp.h \
		pgsql_jobacct_process.c pgsql_jobacct_process.h \
		pgsql_jobcomp_process.c pgsql_jobcomp_process.h
database_pgsql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
database_pgsql_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

