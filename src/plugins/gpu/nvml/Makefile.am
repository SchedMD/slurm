# Makefile for gpu/nvml plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(NVML_CPPFLAGS)

NVML_SOURCES = gpu_nvml.c

pkglib_LTLIBRARIES = gpu_nvml.la
gpu_nvml_la_SOURCES = $(NVML_SOURCES)
gpu_nvml_la_LDFLAGS = $(PLUGIN_FLAGS)
gpu_nvml_la_LIBADD = ../common/libgpu_common.la $(NVML_LIBS)
