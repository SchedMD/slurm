# Makefile for job_submit/throttle plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = job_submit_throttle.la

# Job submit throttle plugin.
job_submit_throttle_la_SOURCES = job_submit_throttle.c
job_submit_throttle_la_LDFLAGS = $(PLUGIN_FLAGS)
