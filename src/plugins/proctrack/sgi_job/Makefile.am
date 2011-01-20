# Makefile for proctrack/sgi_job plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = - -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = proctrack_sgi_job.la

proctrack_sgi_job_la_SOURCES = proctrack_sgi_job.c
proctrack_sgi_job_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(OTHER_FLAGS)
# Don't need to add -ljob because we dlopen the .so to avoid
#  symbol collisions with slurm functions
