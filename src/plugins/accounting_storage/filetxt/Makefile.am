# Makefile for accounting_storage/filetxt plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = accounting_storage_filetxt.la

accounting_storage_filetxt_la_SOURCES = accounting_storage_filetxt.c \
		filetxt_jobacct_process.c filetxt_jobacct_process.h
accounting_storage_filetxt_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
