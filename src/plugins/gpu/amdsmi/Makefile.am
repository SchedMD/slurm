AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) $(AMDSMI_CPPFLAGS)

AMDSMI_SOURCES = gpu_amdsmi.c

pkglib_LTLIBRARIES = gpu_amdsmi.la
gpu_amdsmi_la_SOURCES = $(AMDSMI_SOURCES)
gpu_amdsmi_la_LDFLAGS = $(NUMA_LIBS) $(PLUGIN_FLAGS) 
gpu_amdsmi_la_LIBADD = ../common/libgpu_common.la