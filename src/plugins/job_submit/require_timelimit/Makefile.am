# Makefile for job_submit/require_timelimit plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = job_submit_require_timelimit.la

# Job submit require_timelimit plugin.
job_submit_require_timelimit_la_SOURCES = job_submit_require_timelimit.c
job_submit_require_timelimit_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
