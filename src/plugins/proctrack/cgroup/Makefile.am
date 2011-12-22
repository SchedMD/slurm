# Makefile for proctrack/linuxproc plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER

# Process group ID process tracking plugin.
pkglib_LTLIBRARIES = proctrack_cgroup.la
proctrack_cgroup_la_SOURCES = proctrack_cgroup.c
proctrack_cgroup_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_proctrack_cgroup_la_SOURCES = proctrack_cgroup.c
endif
