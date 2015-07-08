AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = layouts_power_cpufreq_default.la

layouts_power_cpufreq_default_la_SOURCES = default.c
layouts_power_cpufreq_default_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
