# Makefile.am for http_parser_common

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = libhttp_parser_common.la
libhttp_parser_common_la_SOURCES =    \
	http_parser_common.c http_parser_common.h
