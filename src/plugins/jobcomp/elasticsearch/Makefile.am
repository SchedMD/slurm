# Makefile for jobcomp/elasticsearch plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) $(LIBCURL_CPPFLAGS)

depend_libs = $(top_builddir)/src/curl/libslurm_curl.la

AS_ELASTICSEARCH_SOURCES = jobcomp_elasticsearch.c

if WITH_CURL
pkglib_LTLIBRARIES = jobcomp_elasticsearch.la

jobcomp_elasticsearch_la_DEPENDENCIES = $(depend_libs)
jobcomp_elasticsearch_la_SOURCES = $(AS_ELASTICSEARCH_SOURCES)
jobcomp_elasticsearch_la_LDFLAGS = $(PLUGIN_FLAGS)
jobcomp_elasticsearch_la_LIBADD = $(depend_libs) $(LIBCURL) ../common/libjobcomp_common.la

force:
$(jobcomp_elasticsearch_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_jobcomp_elasticsearch_la_SOURCES = $(AS_ELASTICSEARCH_SOURCES)
endif
