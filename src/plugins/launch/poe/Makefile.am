# Makefile for launch/poe plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = launch_poe.la

launch_poe_la_SOURCES = launch_poe.c

launch_poe_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
