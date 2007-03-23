# Makefile for database/flatfile plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = database_flatfile.la

# Flatfile database plugin.
database_flatfile_la_SOURCES = database_flatfile.c \
				flatfile_jobacct.c flatfile_jobacct.h \
				flatfile_jobacct_process.c \
				flatfile_jobacct_process.h
database_flatfile_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
database_flatfile_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

