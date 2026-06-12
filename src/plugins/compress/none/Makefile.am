# Makefile for compress/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = compress_none.la

# Null job credential plugin
compress_none_la_SOURCES = compress_none.c
compress_none_la_LDFLAGS = $(PLUGIN_FLAGS)
