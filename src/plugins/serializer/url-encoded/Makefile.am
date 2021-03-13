# Makefile for serializer/url-encoded plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) \
			  -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = serializer_url_encoded.la

# Serializer URL encoded plugin.
serializer_url_encoded_la_SOURCES = serializer_url_encoded.c
serializer_url_encoded_la_LDFLAGS = $(PLUGIN_FLAGS)

force:
$(serializer_url_encoded_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
