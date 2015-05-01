# Makefile for jobcomp/elasticsearch plugin

AUTOMAKE_OPTIONS = foreign


PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

if WITH_CURL
ELASTICSEARCH = jobcomp_elasticsearch.la
endif

pkglib_LTLIBRARIES = $(ELASTICSEARCH)

# Elasticsearch job completion logging plugin.
jobcomp_elasticsearch_la_SOURCES = jobcomp_elasticsearch.c 

jobcomp_elasticsearch_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

jobcomp_elasticsearch_la_LIBADD = $(LIBCURL)

