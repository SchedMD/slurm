# Makefile for cred/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = cred_none.la

# Null job credential plugin
cred_none_la_SOURCES = cred_none.c
cred_none_la_LDFLAGS = $(PLUGIN_FLAGS)
