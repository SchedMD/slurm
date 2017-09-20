AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = layouts_power_default.la layouts_power_cpufreq.la

layouts_power_default_la_SOURCES = default.c
layouts_power_default_la_LDFLAGS = $(PLUGIN_FLAGS)

layouts_power_cpufreq_la_SOURCES = cpufreq.c
layouts_power_cpufreq_la_LDFLAGS = $(PLUGIN_FLAGS)
