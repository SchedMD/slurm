# Makefile for acct_gather_energy/rsmi plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(RSMI_CPPFLAGS)

pkglib_LTLIBRARIES = acct_gather_energy_rsmi.la

# AMD gpu energy accounting plugin.
acct_gather_energy_rsmi_la_SOURCES = acct_gather_energy_rsmi.c

acct_gather_energy_rsmi_la_LDFLAGS = $(PLUGIN_FLAGS)
