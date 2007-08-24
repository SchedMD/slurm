# Makefile for jobacct-storage/storage/flatfile plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_storage_flatfile.la

# Flatfile storage plugin.
jobacct_storage_flatfile_la_SOURCES = jobacct_storage_flatfile.c \
		flatfile_jobacct_process.c flatfile_jobacct_process.h 
jobacct_storage_flatfile_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobacct_storage_flatfile_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

