# Makefile for jobacct_write/storage/flatfile plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_write_flatfile.la

# Flatfile storage plugin.
storage_flatfile_la_SOURCES = jobacct_write_flatfile.c \
		flatfile_jobacct.c flatfile_jobacct.h \
		flatfile_jobacct_process.c flatfile_jobacct_process.h 
storage_flatfile_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
storage_flatfile_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

