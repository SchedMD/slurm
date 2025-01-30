# Makefile for serializer/xjson plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = serializer_xjson.la

# Serializer xJSON plugin.
serializer_xjson_la_SOURCES = serializer_xjson.c
serializer_xjson_la_LDFLAGS = $(PLUGIN_FLAGS)

force:
$(serializer_xjson_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
