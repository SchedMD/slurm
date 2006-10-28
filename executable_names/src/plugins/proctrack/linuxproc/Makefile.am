# Makefile for proctrack/linuxproc plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = proctrack_linuxproc.la

# Process group ID process tracking  plugin.
proctrack_linuxproc_la_SOURCES = \
	proctrack_linuxproc.c \
	kill_tree.c \
	kill_tree.h
proctrack_linuxproc_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
