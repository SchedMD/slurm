# Makefile for proctrack/linuxproc plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

PROCTRACK_LINUXPROC_LA_SOURCES =	\
	kill_tree.c kill_tree.h		\
	proctrack_linuxproc.c

if ! USE_LOADLEVELER

# Process group ID process tracking  plugin.
pkglib_LTLIBRARIES = proctrack_linuxproc.la
proctrack_linuxproc_la_SOURCES = $(PROCTRACK_LINUXPROC_LA_SOURCES)
proctrack_linuxproc_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_proctrack_linuxproc_la_SOURCES = $(PROCTRACK_LINUXPROC_LA_SOURCES)
endif
