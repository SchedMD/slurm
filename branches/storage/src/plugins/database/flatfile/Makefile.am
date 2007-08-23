# Makefile for storage/flatfile plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = storage_flatfile.la

# Flatfile storage plugin.
storage_flatfile_la_SOURCES = storage_flatfile.c \
		flatfile_jobacct.c flatfile_jobacct.h \
		flatfile_jobcomp.c flatfile_jobcomp.h \
		flatfile_jobacct_process.c flatfile_jobacct_process.h \
		flatfile_jobcomp_process.c flatfile_jobcomp_process.h
storage_flatfile_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
storage_flatfile_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

