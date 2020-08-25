# Makefile for cli_filter/defaults plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = cli_filter_user_defaults.la

# Job submit defaults plugin.
cli_filter_user_defaults_la_SOURCES = cli_filter_user_defaults.c
cli_filter_user_defaults_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
cli_filter_user_defaults_la_LIBADD = ../common/libcli_filter_common.la
