# Makefile for topology/hypercube plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = topology_hypercube.la

topology_hypercube_la_SOURCES = topology_hypercube.c
topology_hypercube_la_LDFLAGS = $(PLUGIN_FLAGS)
