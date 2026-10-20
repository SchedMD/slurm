# Makefile for serializer/json-patch plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = serializer_json_patch.la

# Serializer RFC 6902 JSON Patch plugin.
serializer_json_patch_la_SOURCES = serializer_json_patch.c
serializer_json_patch_la_LDFLAGS = $(PLUGIN_FLAGS)

force:
$(serializer_json_patch_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
