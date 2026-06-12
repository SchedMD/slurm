# Makefile for OpenMetrics plugin

AUTOMAKE_OPTIONS = foreign
PLUGIN_FLAGS = -module -avoid-version --export-dynamic
AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)
OPENMETRICS_SOURCES = openmetrics.h openmetrics.c

pkglib_LTLIBRARIES = metrics_openmetrics.la
metrics_openmetrics_la_SOURCES = $(OPENMETRICS_SOURCES)
metrics_openmetrics_la_LDFLAGS = $(PLUGIN_FLAGS)
