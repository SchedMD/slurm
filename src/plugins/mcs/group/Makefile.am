# Makefile for mcs/group plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mcs_group.la

# mcs/group plugin.
mcs_group_la_SOURCES = mcs_group.c
mcs_group_la_LDFLAGS = $(PLUGIN_FLAGS)
