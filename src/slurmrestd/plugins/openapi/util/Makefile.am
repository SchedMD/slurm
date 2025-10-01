# Makefile for openapi/util plugin

AUTOMAKE_OPTIONS = foreign
CLEANFILES = *.bino

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) \
	      -I$(top_srcdir)/src/slurmrestd/plugins/openapi/util/

pkglib_LTLIBRARIES = openapi_util.la

openapi_util_la_SOURCES = \
	api.c api.h

openapi_util_la_LDFLAGS = $(PLUGIN_FLAGS)
