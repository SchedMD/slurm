# Makefile for switch/nrt plugin (NRT = IBM's Network Routing Table)

CPPFLAGS =  -DFEDERATION_CONFIG_FILE=\"$(sysconfdir)/nrt.conf\"
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

SWITCH_NRT_LA_SOURCES =		\
	nrt.c nrt.h		\
	nrt_keys.h		\
	switch_nrt.c

if HAVE_NRT
pkglib_LTLIBRARIES = switch_nrt.la
switch_nrt_la_SOURCES = $(SWITCH_NRT_LA_SOURCES)
switch_nrt_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(NRT_LDFLAGS)

else
pkglib_LTLIBRARIES =
EXTRA_switch_nrt_la_SOURCES = $(SWITCH_NRT_LA_SOURCES)
endif
