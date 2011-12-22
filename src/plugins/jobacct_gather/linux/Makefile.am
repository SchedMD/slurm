# Makefile for jobacct_gather/linux plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER

# Null job completion logging plugin.
pkglib_LTLIBRARIES = jobacct_gather_linux.la
jobacct_gather_linux_la_SOURCES = jobacct_gather_linux.c
jobacct_gather_linux_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_jobacct_gather_linux_la_SOURCES = jobacct_gather_linux.c
endif
