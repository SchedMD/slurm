# Makefile for proctrack/cray_aries plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = - -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(CRAY_JOB_CPPFLAGS)

pkglib_LTLIBRARIES = proctrack_cray_aries.la

proctrack_cray_aries_la_SOURCES = proctrack_cray_aries.c
proctrack_cray_aries_la_LDFLAGS = $(PLUGIN_FLAGS) $(CRAY_JOB_LDFLAGS)
