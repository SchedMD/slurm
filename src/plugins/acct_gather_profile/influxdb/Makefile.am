# Makefile for acct_gather_profile/influxdb plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) $(LIBCURL_CPPFLAGS)

pkglib_LTLIBRARIES = acct_gather_profile_influxdb.la

acct_gather_profile_influxdb_la_SOURCES = acct_gather_profile_influxdb.c
acct_gather_profile_influxdb_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
acct_gather_profile_influxdb_la_LIBADD = $(LIBCURL)
