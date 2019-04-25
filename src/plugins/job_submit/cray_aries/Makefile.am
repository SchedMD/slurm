# Makefile for job_submit/cray_aries plugin

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DDEFAULT_SCRIPT_DIR=\"$(sysconfdir)\" \
	-I$(top_srcdir) -I$(top_srcdir)/src/common

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

pkglib_LTLIBRARIES = job_submit_cray_aries.la
job_submit_cray_aries_la_SOURCES = job_submit_cray_aries.c
job_submit_cray_aries_la_LDFLAGS = $(PLUGIN_FLAGS)

