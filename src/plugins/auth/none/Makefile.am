# Makefile for auth/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = auth_none.la

# Null authentication plugin.
auth_none_la_SOURCES = auth_none.c
auth_none_la_LDFLAGS = $(PLUGIN_FLAGS)
