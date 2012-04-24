# Makefile for launch/runjob plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = launch_runjob.la

launch_runjob_la_SOURCES = launch_runjob.c

launch_runjob_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
