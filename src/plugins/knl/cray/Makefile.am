# Makefile for knl/cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

if WITH_JSON_PARSER

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(JSON_CPPFLAGS)

pkglib_LTLIBRARIES = knl_cray.la
knl_cray_la_SOURCES = knl_cray.c
knl_cray_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(JSON_LDFLAGS)

force:
$(knl_cray_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_knl_cray_la_SOURCES = knl_cray.c
endif
