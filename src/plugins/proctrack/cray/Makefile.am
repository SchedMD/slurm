# Makefile for proctrack/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = - -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = proctrack_cray.la

proctrack_cray_la_SOURCES = proctrack_cray.c
proctrack_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(OTHER_FLAGS)
# Don't need to add -ljob because we dlopen the .so to avoid
#  symbol collisions with slurm functions
