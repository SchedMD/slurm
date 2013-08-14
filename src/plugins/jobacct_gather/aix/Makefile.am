# Makefile for jobacct_gather/aix plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_gather_aix.la

# Null job completion logging plugin.
jobacct_gather_aix_la_SOURCES = jobacct_gather_aix.c

jobacct_gather_aix_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

jobacct_gather_aix_la_LIBADD = ../common/libjobacct_gather_common.la

force:
$(jobacct_gather_aix_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
