# Makefile for job_submit/pbs and spank/pbs plugins

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

# Job submit PBS plugin.
pkglib_LTLIBRARIES = job_submit_pbs.la spank_pbs.la
job_submit_pbs_la_SOURCES = job_submit_pbs.c
job_submit_pbs_la_LDFLAGS = $(PLUGIN_FLAGS)

# SPANK PBS plugin.
spank_pbs_la_SOURCES = spank_pbs.c
spank_pbs_la_LDFLAGS = $(PLUGIN_FLAGS)
