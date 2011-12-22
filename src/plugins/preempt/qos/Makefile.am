# Makefile for preempt/qos plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if ! USE_LOADLEVELER

pkglib_LTLIBRARIES = preempt_qos.la
preempt_qos_la_SOURCES = preempt_qos.c
preempt_qos_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
EXTRA_preempt_qos_la_SOURCES = preempt_qos.c
endif
