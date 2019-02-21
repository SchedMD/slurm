# Makefile for gpu/nvml plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(NVML_CPPFLAGS)

pkglib_LTLIBRARIES = gpu_nvml.la

# GPU NVML plugin.
gpu_nvml_la_SOURCES = gpu_nvml.c
gpu_nvml_la_LDFLAGS = $(PLUGIN_FLAGS)
gpu_nvml_la_LIBADD = $(NVML_LIBS)

force:
$(gpu_nvml_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
