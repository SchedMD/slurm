# Makefile for priority/multifactor2 plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version -export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = priority_multifactor2.la

priority_multifactor2_la_SOURCES = priority_multifactor2.c
priority_multifactor2_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
priority_multifactor2_la_LIBADD  = -lm
