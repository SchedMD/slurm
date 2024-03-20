# Makefile for gpu/hlml plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(HLML_CPPFLAGS)

HLML_SOURCES = gpu_hlml.c

pkglib_LTLIBRARIES = gpu_hlml.la
gpu_hlml_la_SOURCES = $(HLML_SOURCES)
gpu_hlml_la_LDFLAGS = $(NUMA_LIBS) $(PLUGIN_FLAGS)
gpu_hlml_la_LIBADD = ../common/libgpu_common.la

