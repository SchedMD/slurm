# Makefile for preempt/job_prio plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = preempt_job_prio.la

preempt_job_prio_la_SOURCES = preempt_job_prio.c

preempt_job_prio_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

