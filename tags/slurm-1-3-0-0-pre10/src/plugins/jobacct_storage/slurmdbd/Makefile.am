# Makefile for jobacct_storage/slurmdbd plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_storage_slurmdbd.la

# Null job completion logging plugin.
jobacct_storage_slurmdbd_la_SOURCES = jobacct_storage_slurmdbd.c
jobacct_storage_slurmdbd_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobacct_storage_slurmdbd_la_LIBADD  = $(top_builddir)/src/common/libcommon.la
