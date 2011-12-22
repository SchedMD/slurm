# Makefile for switch/federation plugin

CPPFLAGS =  -DFEDERATION_CONFIG_FILE=\"$(sysconfdir)/federation.conf\"
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

SWITCH_FEDERATION_LA_SOURCES =		\
	federation.c federation.h	\
	federation_keys.h		\
	switch_federation.c

if BUILD_FEDERATION
pkglib_LTLIBRARIES = switch_federation.la
switch_federation_la_SOURCES = $(SWITCH_FEDERATION_LA_SOURCES)
switch_federation_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) \
	 $(FEDERATION_LDFLAGS)

else
pkglib_LTLIBRARIES =
EXTRA_switch_federation_la_SOURCES = $(SWITCH_FEDERATION_LA_SOURCES)
endif
