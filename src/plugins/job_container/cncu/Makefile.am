# Makefile for job_container/cncu plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(CRAY_JOB_CPPFLAGS)

pkglib_LTLIBRARIES = job_container_cncu.la

# job_container/cncu plugin.
job_container_cncu_la_SOURCES = job_container_cncu.c
job_container_cncu_la_LDFLAGS = $(PLUGIN_FLAGS) \
	$(CRAY_JOB_LDFLAGS)

