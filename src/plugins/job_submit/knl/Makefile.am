# Makefile for job_submit/knl plugin

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DDEFAULT_SCRIPT_DIR=\"$(sysconfdir)\" \
	-I$(top_srcdir) -I$(top_srcdir)/src/common

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

pkglib_LTLIBRARIES = job_submit_knl.la
job_submit_knl_la_SOURCES = job_submit_knl.c
job_submit_knl_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
