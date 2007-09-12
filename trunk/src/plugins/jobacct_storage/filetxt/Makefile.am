# Makefile for jobacct-storage/filetxt plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_storage_filetxt.la

# Filetxt storage plugin.
jobacct_storage_filetxt_la_SOURCES = jobacct_storage_filetxt.c \
		filetxt_jobacct_process.c filetxt_jobacct_process.h 
jobacct_storage_filetxt_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
jobacct_storage_filetxt_la_LIBADD  = $(top_builddir)/src/api/libslurmhelper.la
