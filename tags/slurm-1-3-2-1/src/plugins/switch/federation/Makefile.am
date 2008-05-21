# Makefile for switch/federation plugin

CPPFLAGS =  -DFEDERATION_CONFIG_FILE=\"$(sysconfdir)/federation.conf\"
AUTOMAKE_OPTIONS = foreign

if HAVE_FEDERATION
federation_lib = switch_federation.la
else
federation_lib = 
endif

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = $(federation_lib)

if HAVE_FEDERATION
# Federation switch plugin.
switch_federation_la_SOURCES = \
	federation.c federation.h \
	federation_keys.h \
	switch_federation.c 
switch_federation_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) \
	 $(FEDERATION_LDFLAGS)

else
EXTRA_switch_federation_la_SOURCES = \
	federation.c federation.h \
	federation_keys.h \
	switch_federation.c
endif
