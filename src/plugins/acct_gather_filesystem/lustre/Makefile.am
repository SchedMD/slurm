# Makefile for acct_gather_filesystem/lustre plugin

#dprx need to uncomment this when safe to build
#SUBDIRS = include lib

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

pkglib_LTLIBRARIES = acct_gather_filesystem_lustre.la

# Infiniband accounting lustre plugin.
acct_gather_filesystem_lustre_la_SOURCES = acct_gather_filesystem_lustre.c

acct_gather_filesystem_lustre_la_LDFLAGS = $(PLUGIN_FLAGS)
