# Makefile for job_container/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = job_container_none.la

# job_container/none plugin.
job_container_none_la_SOURCES = job_container_none.c
job_container_none_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
