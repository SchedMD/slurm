# Makefile for node_features_knl_cray plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

if WITH_JSON_PARSER

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(DATAWARP_CPPFLAGS) $(JSON_CPPFLAGS)

pkglib_LTLIBRARIES = node_features_knl_cray.la
node_features_knl_cray_la_SOURCES = node_features_knl_cray.c
node_features_knl_cray_la_LDFLAGS = $(PLUGIN_FLAGS) $(DATAWARP_LDFLAGS) $(JSON_LDFLAGS)

force:
$(node_features_knl_cray_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_node_features_knl_cray_la_SOURCES = node_features_knl_cray.c
endif
