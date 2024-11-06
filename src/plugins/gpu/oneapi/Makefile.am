# Makefile for gpu/oneapi plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(ONEAPI_CPPFLAGS)

ONEAPI_SOURCES = gpu_oneapi.c

pkglib_LTLIBRARIES = gpu_oneapi.la
gpu_oneapi_la_SOURCES = $(ONEAPI_SOURCES)
gpu_oneapi_la_LDFLAGS = $(PLUGIN_FLAGS)
gpu_oneapi_la_LIBADD = ../common/libgpu_common.la
