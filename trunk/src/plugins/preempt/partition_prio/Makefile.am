# Makefile for preempt/partition_prio plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = preempt_partition_prio.la

preempt_partition_prio_la_SOURCES = preempt_partition_prio.c

preempt_partition_prio_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

