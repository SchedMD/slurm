# Makefile for acct_gather_energy/pm_counters plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = acct_gather_energy_pm_counters.la

# cpu/core energy accounting plugin.
acct_gather_energy_pm_counters_la_SOURCES = acct_gather_energy_pm_counters.c

acct_gather_energy_pm_counters_la_LDFLAGS = $(PLUGIN_FLAGS)
