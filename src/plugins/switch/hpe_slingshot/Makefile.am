# Makefile for switch/hpe_slingshot plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common \
	      $(HPE_SLINGSHOT_CFLAGS) $(LIBCURL_CPPFLAGS) $(JSON_CPPFLAGS)

if WITH_JSON_PARSER
if WITH_CURL

pkglib_LTLIBRARIES = switch_hpe_slingshot.la

# HPE Slingshot switch plugin.
switch_hpe_slingshot_la_SOURCES = \
	apinfo.c \
	config.c \
	instant_on.c \
	setup_nic.c \
	switch_hpe_slingshot.c \
	switch_hpe_slingshot.h
switch_hpe_slingshot_la_LDFLAGS = $(PLUGIN_FLAGS)
switch_hpe_slingshot_la_LIBADD = $(LIBCURL) $(JSON_LDFLAGS)

endif
endif
