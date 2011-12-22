# Makefile for gres/gpu plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(HWLOC_CPPFLAGS)

if ! USE_LOADLEVELER

# Gres GPU plugin.
pkglib_LTLIBRARIES = gres_gpu.la
gres_gpu_la_SOURCES = gres_gpu.c
gres_gpu_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(HWLOC_LDFLAGS)
gres_gpu_la_LIBADD  = $(HWLOC_LIBS)

else
EXTRA_gres_gpu_la_SOURCES = gres_gpu.c
endif
