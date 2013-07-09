# Makefile for job_submit/cnode plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = job_submit_cnode.la

# Job submit cnode plugin.
job_submit_cnode_la_SOURCES = job_submit_cnode.c
job_submit_cnode_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
