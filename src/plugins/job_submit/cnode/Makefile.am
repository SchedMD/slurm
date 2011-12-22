# Makefile for job_submit/cnode plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER

# Job submit cnode plugin.
pkglib_LTLIBRARIES = job_submit_cnode.la
job_submit_cnode_la_SOURCES = job_submit_cnode.c
job_submit_cnode_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_job_submit_cnode_la_SOURCES = job_submit_cnode.c
endif
