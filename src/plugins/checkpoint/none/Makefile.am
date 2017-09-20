# Makefile for checkpoint/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = checkpoint_none.la

# Null checkpoint plugin.
checkpoint_none_la_SOURCES = checkpoint_none.c
checkpoint_none_la_LDFLAGS = $(PLUGIN_FLAGS)
