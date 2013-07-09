# Makefile for switch/nrt plugin (NRT = IBM's Network Routing Table)

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(NRT_CPPFLAGS)


SWITCH_NRT_LA_SOURCES =		\
	nrt.c			\
	nrt_keys.h		\
	slurm_nrt.h		\
	switch_nrt.c

if HAVE_NRT

SUBDIRS = libpermapi

pkglib_LTLIBRARIES = switch_nrt.la

switch_nrt_la_SOURCES = $(SWITCH_NRT_LA_SOURCES)
switch_nrt_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

else
pkglib_LTLIBRARIES =
EXTRA_switch_nrt_la_SOURCES = $(SWITCH_NRT_LA_SOURCES)
endif
