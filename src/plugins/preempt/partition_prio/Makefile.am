# Makefile for preempt/partition_prio plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = preempt_partition_prio.la

preempt_partition_prio_la_SOURCES = preempt_partition_prio.c

preempt_partition_prio_la_LDFLAGS = $(PLUGIN_FLAGS)

