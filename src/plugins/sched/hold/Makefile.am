# Makefile for hold scheduler plugin

AUTOMAKE_OPTIONS = foreign

CXXFLAGS = -fexceptions

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = sched_hold.la

sched_hold_la_SOURCES = hold_wrapper.c
sched_hold_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
