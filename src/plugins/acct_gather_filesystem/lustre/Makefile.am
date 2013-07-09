# Makefile for acct_gather_filesystem/lustre plugin

#dprx need to uncomment this when safe to build
#SUBDIRS = include lib

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

pkglib_LTLIBRARIES = acct_gather_filesystem_lustre.la

# Infiniband accounting lustre plugin.
acct_gather_filesystem_lustre_la_SOURCES = acct_gather_filesystem_lustre.c

acct_gather_filesystem_lustre_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
acct_gather_filesystem_lustre_la_LIBADD = -lm
