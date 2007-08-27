# Makefile for jobacct-storage/pgsql plugin

CPPFLAGS = $(PGSQL_CFLAGS)
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic $(PGSQL_LIBS)

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_storage_pgsql.la

# Pgsql storage plugin.
jobacct_storage_pgsql_la_SOURCES = jobacct_storage_pgsql.c \
		pgsql_jobacct_process.c pgsql_jobacct_process.h 
jobacct_storage_pgsql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobacct_storage_pgsql_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

