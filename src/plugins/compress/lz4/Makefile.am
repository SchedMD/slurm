# Makefile for compress/lz4 plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = compress_lz4.la
compress_lz4_la_SOURCES = compress_lz4.c
compress_lz4_la_LIBADD = $(LZ4_LIBS)
compress_lz4_la_LDFLAGS = $(PLUGIN_FLAGS) $(LZ4_LDFLAGS)
compress_lz4_la_CFLAGS = $(LZ4_CPPFLAGS) $(AM_CPPFLAGS)
