# Makefile for clusteracct_storage/slurmdbd plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = clusteracct_storage_slurmdbd.la

# Null job completion logging plugin.
clusteracct_storage_slurmdbd_la_SOURCES = clusteracct_storage_slurmdbd.c
clusteracct_storage_slurmdbd_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
clusteracct_storage_slurmdbd_la_LIBADD  = $(top_builddir)/src/common/libcommon.la
