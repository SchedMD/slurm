# Makefile for job_submit/resilience plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = job_submit_resilience.la

job_submit_resilience_la_SOURCES = job_submit_resilience.c
job_submit_resilience_la_LDFLAGS = $(PLUGIN_FLAGS)
