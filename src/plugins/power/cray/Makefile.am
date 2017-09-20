# Makefile for power/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

if WITH_JSON_PARSER

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(JSON_CPPFLAGS)

pkglib_LTLIBRARIES = power_cray.la
power_cray_la_SOURCES = power_cray.c
power_cray_la_LDFLAGS = $(PLUGIN_FLAGS) $(JSON_LDFLAGS)
power_cray_la_LIBADD = ../common/libpower_common.la

force:
$(power_cray_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_power_cray_la_SOURCES = power_cray.c
endif
