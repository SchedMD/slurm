# Makefile for acct_gather_profile/influxdb plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(LIBCURL_CPPFLAGS)

INFLUXDB_SOURCES = acct_gather_profile_influxdb.c

if WITH_CURL
pkglib_LTLIBRARIES = acct_gather_profile_influxdb.la

acct_gather_profile_influxdb_la_SOURCES = $(INFLUXDB_SOURCES)
acct_gather_profile_influxdb_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
acct_gather_profile_influxdb_la_LIBADD = $(LIBCURL)

else
EXTRA_acct_gather_profile_influxdb_la_SOURCES = $(INFLUXDB_SOURCES)
endif
