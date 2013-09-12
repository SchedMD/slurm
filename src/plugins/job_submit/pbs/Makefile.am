# Makefile for job_submit/pbs plugin

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DDEFAULT_SCRIPT_DIR=\"$(sysconfdir)\"

PLUGIN_FLAGS = -module -avoid-version --export-dynamic
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common
 
# Job submit pbs plugin.
pkglib_LTLIBRARIES = job_submit_pbs.la
job_submit_pbs_la_SOURCES = job_submit_pbs.c
job_submit_pbs_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
