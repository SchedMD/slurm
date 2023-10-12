# Makefile for gpu/nrt plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcidr)/src/common $(NRT_CPPFLAGS)

NRT_SOURCES = gpu_nrt.c

pkglib_LTLIBRARIES = gpu_nrt.la
gpu_nrt_la_SOURCES = $(NRT_SOURCES)
gpu_nrt_la_LDFLAGS = $(PLUGIN_FLAGS)
