# Makefile.am for cli_filter/common

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = libcli_filter_common.la
libcli_filter_common_la_SOURCES =	\
	cli_filter_common.c
