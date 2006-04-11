# $Id: Makefile.am 7555 2006-03-21 18:00:02Z da $
# Makefile for jobacct/bluegene plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_bluegene.la

# Null job completion logging plugin.
jobacct_bluegene_la_SOURCES = jobacct_bluegene.c \
	$(top_builddir)/src/plugins/jobacct/common/jobacct_common.c \
	$(top_builddir)/src/plugins/jobacct/common/common_slurmctld.c \
	$(top_builddir)/src/plugins/jobacct/common/common_slurmstepd.c
jobacct_bluegene_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
