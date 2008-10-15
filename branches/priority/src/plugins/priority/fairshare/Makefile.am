# Makefile for priority/fairshare plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = priority_fairshare.la

# Null priority logging plugin.
priority_fairshare_la_SOURCES = priority_fairshare.c
priority_fairshare_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
