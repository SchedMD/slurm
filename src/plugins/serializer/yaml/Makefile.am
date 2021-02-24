# Makefile for serializer/yaml plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) \
			  -I$(top_srcdir)/src/common

if WITH_YAML

pkglib_LTLIBRARIES = serializer_yaml.la

# Serializer yaml plugin.
serializer_yaml_la_SOURCES = serializer_yaml.c
serializer_yaml_la_LDFLAGS = $(PLUGIN_FLAGS)
serializer_yaml_la_LIBADD = $(YAML_LDFLAGS)

force:
$(serializer_yaml_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

endif
