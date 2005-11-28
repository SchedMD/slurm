# $Id$
# Makefile for jobacct/log plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_log.la

# Null job completion logging plugin.
jobacct_log_la_SOURCES = jobacct_log.c
jobacct_log_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
