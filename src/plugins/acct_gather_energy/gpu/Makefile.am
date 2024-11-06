# Makefile for acct_gather_energy/gpu plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = acct_gather_energy_gpu.la

# AMD gpu energy accounting plugin.
acct_gather_energy_gpu_la_SOURCES = acct_gather_energy_gpu.c

acct_gather_energy_gpu_la_LDFLAGS = $(PLUGIN_FLAGS)
