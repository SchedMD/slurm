# Makefile for url_parser/internal plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

pkglib_LTLIBRARIES = url_parser_internal.la
url_parser_internal_la_SOURCES = url_parser_internal.c \
	http_parser_url_port.c http_parser_url_port.h
url_parser_internal_la_LDFLAGS = $(PLUGIN_FLAGS)
