# Makefile for acct_gather_energy/cray_aries plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = acct_gather_energy_cray_aries.la

# cpu/core energy accounting plugin.
acct_gather_energy_cray_aries_la_SOURCES = acct_gather_energy_cray_aries.c

acct_gather_energy_cray_aries_la_LDFLAGS = $(PLUGIN_FLAGS)
acct_gather_energy_cray_aries_la_LIBADD  = -lm
