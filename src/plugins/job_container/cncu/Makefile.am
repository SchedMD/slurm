# Makefile for job_container/cncu plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(CRAY_CPPFLAGS)

if HAVE_REAL_CRAY
CNCU_PLUGIN = job_container_cncu.la
endif
pkglib_LTLIBRARIES = $(CNCU_PLUGIN)

# job_container/cncu plugin.
job_container_cncu_la_SOURCES = job_container_cncu.c
job_container_cncu_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(CRAY_LDFLAGS)
