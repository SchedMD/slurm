# Makefile for jobacct_storage/script plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_storage_script.la

# Script storage plugin.
storage_script_la_SOURCES = jobacct_storage_script.c \
		script_jobacct.c script_jobacct.h \
		script_jobacct_process.c script_jobacct_process.h 
storage_script_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
storage_script_la_LIBADD  = $(top_builddir)/src/common/libcommon.la

