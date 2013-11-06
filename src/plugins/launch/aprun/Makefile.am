# Makefile for launch/aprun plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = launch_aprun.la

launch_aprun_la_SOURCES = launch_aprun.c

launch_aprun_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
