# Makefile for proctrack/cgroup plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = proctrack_cgroup.la

# Process group ID process tracking plugin.
proctrack_cgroup_la_SOURCES = proctrack_cgroup.c
proctrack_cgroup_la_LDFLAGS = $(PLUGIN_FLAGS)
