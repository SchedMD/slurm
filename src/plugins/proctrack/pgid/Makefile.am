# Makefile for proctrack/pgid plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = proctrack_pgid.la

# Process group ID process tracking  plugin.
proctrack_pgid_la_SOURCES = proctrack_pgid.c
proctrack_pgid_la_LDFLAGS = $(PLUGIN_FLAGS)
