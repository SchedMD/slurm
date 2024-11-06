# Makefile for acct_gather_interconnect/sysfs plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = acct_gather_interconnect_sysfs.la

acct_gather_interconnect_sysfs_la_SOURCES = acct_gather_interconnect_sysfs.c
acct_gather_interconnect_sysfs_la_LDFLAGS = $(PLUGIN_FLAGS)
