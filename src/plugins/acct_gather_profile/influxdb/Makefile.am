# Makefile for acct_gather_profile/influxdb plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

# Do not put a link to common here.  src/common contains an mpi.h which
# hdf5 could of been installed with a link to the generic mpi.h.
AM_CPPFLAGS = -I$(top_srcdir)

# cpu/core energy accounting plugin.

if WITH_CURL
pkglib_LTLIBRARIES = acct_gather_profile_influxdb.la
endif


acct_gather_profile_influxdb_la_SOURCES = acct_gather_profile_influxdb.c
acct_gather_profile_influxdb_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) 
acct_gather_profile_influxdb_la_LIBADD = $(LIBCURL) 
