# Makefile for job_submit/partition plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = job_submit_partition.la

# Job submit partition plugin.
job_submit_partition_la_SOURCES = job_submit_partition.c
job_submit_partition_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
