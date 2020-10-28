# Makefile for priority/multifactor plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = priority_multifactor.la

priority_multifactor_la_SOURCES = priority_multifactor.c fair_tree.c fair_tree.h priority_multifactor.h

priority_multifactor_la_LDFLAGS = $(PLUGIN_FLAGS)
