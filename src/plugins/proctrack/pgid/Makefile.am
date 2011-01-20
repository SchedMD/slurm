# Makefile for proctrack/pgid plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = proctrack_pgid.la

# Process group ID process tracking  plugin.
proctrack_pgid_la_SOURCES = proctrack_pgid.c
proctrack_pgid_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
