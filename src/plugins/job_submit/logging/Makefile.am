# Makefile for job_submit/logging plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = job_submit_logging.la

# Job submit logging plugin.
job_submit_logging_la_SOURCES = job_submit_logging.c
job_submit_logging_la_LDFLAGS = $(PLUGIN_FLAGS)
