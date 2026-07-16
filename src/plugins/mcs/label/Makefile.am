# Makefile for mcs/label plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = mcs_label.la

# mcs/label plugin.
mcs_label_la_SOURCES = mcs_label.c
mcs_label_la_LDFLAGS = $(PLUGIN_FLAGS)
