# Makefile for job_container/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(CRAY_JOB_CPPFLAGS)

if HAVE_REAL_CRAY
CRAY_JOB = job_container_cray.la
endif
pkglib_LTLIBRARIES = $(CRAY_JOB)

# job_container/cray plugin.
job_container_cray_la_SOURCES = job_container_cray.c
job_container_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(CRAY_JOB_LDFLAGS)
job_container_cray_la_LIBADD  = $(CRAY_JOB_LIBS)
