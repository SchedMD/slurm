# Makefile for gpu/amd plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

AMD_SOURCES = gpu_amd.c

pkglib_LTLIBRARIES = gpu_amd.la
gpu_amd_la_SOURCES = $(AMD_SOURCES)
gpu_amd_la_LDFLAGS = $(PLUGIN_FLAGS)
gpu_amd_la_LIBADD = ../common/libgpu_common.la
