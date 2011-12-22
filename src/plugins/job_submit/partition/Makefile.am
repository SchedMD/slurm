# Makefile for job_submit/partition plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER

# Job submit partition plugin.
pkglib_LTLIBRARIES = job_submit_partition.la
job_submit_partition_la_SOURCES = job_submit_partition.c
job_submit_partition_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_job_submit_partition_la_SOURCES = job_submit_partition.c
endif
