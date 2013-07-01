# Makefile for launch/slurm plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = launch_slurm.la

launch_slurm_la_SOURCES = launch_slurm.c task_state.c

launch_slurm_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
