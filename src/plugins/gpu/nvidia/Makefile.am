# Makefile for gpu/nvidia plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

NVIDIA_SOURCES = gpu_nvidia.c

pkglib_LTLIBRARIES = gpu_nvidia.la
gpu_nvidia_la_SOURCES = $(NVIDIA_SOURCES)
gpu_nvidia_la_LDFLAGS = $(PLUGIN_FLAGS)
gpu_nvidia_la_LIBADD = ../common/libgpu_common.la
