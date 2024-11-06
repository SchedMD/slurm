# Makefile for acct_gather_profile/influxdb plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) $(LIBCURL_CPPFLAGS)

depend_libs = $(top_builddir)/src/curl/libslurm_curl.la

pkglib_LTLIBRARIES = acct_gather_profile_influxdb.la

acct_gather_profile_influxdb_la_DEPENDENCIES = $(depend_libs)
acct_gather_profile_influxdb_la_SOURCES = acct_gather_profile_influxdb.c
acct_gather_profile_influxdb_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
acct_gather_profile_influxdb_la_LIBADD = $(depend_libs) $(LIBCURL)

force:
$(acct_gather_profile_influxdb_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`