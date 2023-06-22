# Makefile for serializer/json plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) \
			  -I$(top_srcdir)/src/common $(JSON_CPPFLAGS)

if WITH_JSON_PARSER

pkglib_LTLIBRARIES = serializer_json.la

# Serializer JSON plugin.
serializer_json_la_SOURCES = serializer_json.c
serializer_json_la_LDFLAGS = $(PLUGIN_FLAGS)
serializer_json_la_LIBADD = $(JSON_LDFLAGS)

force:
$(serializer_json_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

endif
