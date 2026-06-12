# Makefile for switch/hpe_slingshot plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) \
	      $(HPE_SLINGSHOT_CFLAGS) $(LIBCURL_CPPFLAGS) $(JSON_CPPFLAGS)

if WITH_JSON_PARSER
if WITH_CURL

depend_libs = $(top_builddir)/src/curl/libslurm_curl.la

pkglib_LTLIBRARIES = switch_hpe_slingshot.la

# HPE Slingshot switch plugin.
switch_hpe_slingshot_la_DEPENDENCIES = $(depend_libs)
switch_hpe_slingshot_la_SOURCES = \
	apinfo.c \
	collectives.c \
	config.c \
	rest.c \
	rest.h \
	setup_nic.c \
	switch_hpe_slingshot.c \
	switch_hpe_slingshot.h
switch_hpe_slingshot_la_LDFLAGS = $(PLUGIN_FLAGS)
switch_hpe_slingshot_la_LIBADD = $(depend_libs) $(LIBCURL) $(JSON_LDFLAGS)

force:
$(switch_hpe_slingshot_la_LIBADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

endif
endif
