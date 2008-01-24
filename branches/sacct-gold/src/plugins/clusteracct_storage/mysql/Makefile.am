# Makefile for clusteracct_storage/mysql plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = clusteracct_storage_mysql.la

# Null job completion logging plugin.
clusteracct_storage_mysql_la_SOURCES = clusteracct_storage_mysql.c
clusteracct_storage_mysql_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
clusteracct_storage_mysql_la_CFLAGS = $(MYSQL_CFLAGS)
clusteracct_storage_mysql_la_LIBADD =  \
	$(top_builddir)/src/database/libslurm_mysql.la $(MYSQL_LIBS)
clusteracct_storage_mysql_la_DEPENDENCIES = \
	$(top_builddir)/src/database/libslurm_mysql.la
