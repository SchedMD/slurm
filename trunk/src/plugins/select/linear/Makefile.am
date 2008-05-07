# Makefile for select/linear plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = select_linear.la

# Linear node selection plugin.
select_linear_la_SOURCES = select_linear.c select_linear.h
select_linear_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

# Add libcommon to provide some symbols that are not
#  available in slurmctld (slurm_get_avail_procs)

select_linear_la_LIBADD  = $(top_builddir)/src/common/.libs/libcommon.a

