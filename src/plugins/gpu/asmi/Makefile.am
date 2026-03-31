# Makefile for gpu/asmi plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) $(AMDSMI_CPPFLAGS)

AMDSMI_SOURCES = gpu_asmi.c

pkglib_LTLIBRARIES = libgpu_asmi.la
gpu_asmi_la_SOURCES = $(AMDSMI_SOURCES)
gpu_asmi_la_LDFLAGS = $(NUMA_LIBS) $(PLUGIN_FLAGS)
gpu_asmi_la_LIBADD = ../common/libgpu_common.la
if BUILD_ASMI
pkglib_LTLIBRARIES = libgpu_asmi.la

libslurm_amdsmi_la_SOURCES  = gpu_asmi.c
libslurm_amdsmi_la_CPPFLAGS = $(AMDSMI_CPPFLAGS) $(AM_CPPFLAGS)
libslurm_amdsmi_la_LDFLAGS  = $(AM_LDFLAGS)
libslurm_amdsmi_la_LIBADD   = $(AMDSMI_LIBS)
endif