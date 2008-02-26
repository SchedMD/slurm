# Makefile for account_storage/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = account_storage_none.la

account_storage_none_la_SOURCES = account_storage_none.c
account_storage_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
