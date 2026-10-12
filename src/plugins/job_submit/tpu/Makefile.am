# Makefile for job_submit/tpu plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = job_submit_tpu.la

# Job submit TPU plugin.
job_submit_tpu_la_SOURCES = job_submit_tpu.c
job_submit_tpu_la_LDFLAGS = $(PLUGIN_FLAGS)
