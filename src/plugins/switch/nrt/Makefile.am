# Makefile for switch/nrt plugin (NRT = IBM's Network Routing Table)

CPPFLAGS =  -DNRT_CONFIG_FILE=\"$(sysconfdir)/nrt.conf\"
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

convenience_libs = $(top_builddir)/src/api/libslurm.o -ldl

SWITCH_NRT_LA_SOURCES =		\
	nrt.c			\
	nrt_keys.h		\
	slurm_nrt.h		\
	switch_nrt.c

if HAVE_NRT
pkglib_LTLIBRARIES = switch_nrt.la libpermapi.la
switch_nrt_la_SOURCES = $(SWITCH_NRT_LA_SOURCES)
switch_nrt_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(NRT_LDFLAGS)
libpermapi_la_SOURCES = shr_64.c
libpermapi_la_LDFLAGS = -export-dynamic $(SO_LDFLAGS) $(PLUGIN_FLAGS) \
	$(convenience_libs) -lpthread
else
pkglib_LTLIBRARIES =
EXTRA_switch_nrt_la_SOURCES = $(SWITCH_NRT_LA_SOURCES)
endif
