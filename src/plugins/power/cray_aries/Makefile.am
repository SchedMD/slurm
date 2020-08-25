# Makefile for power/cray_aries plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

if WITH_JSON_PARSER

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(JSON_CPPFLAGS)

pkglib_LTLIBRARIES = power_cray_aries.la
power_cray_aries_la_SOURCES = power_cray_aries.c
power_cray_aries_la_LDFLAGS = $(PLUGIN_FLAGS) $(JSON_LDFLAGS)
power_cray_aries_la_LIBADD = ../common/libpower_common.la

force:
$(power_cray_aries_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_power_cray_aries_la_SOURCES = power_cray_aries.c
endif
