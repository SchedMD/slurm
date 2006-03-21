# $Id$
# Makefile for jobacct/linux plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_linux.la

# Null job completion logging plugin.
jobacct_linux_la_SOURCES = jobacct_linux.c
jobacct_linux_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
