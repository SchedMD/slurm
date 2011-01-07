# Makefile for select/bgq plugin

CPPFLAGS = -DBLUEGENE_CONFIG_FILE=\"$(sysconfdir)/bluegene.conf\"
AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(BG_INCLUDES)

pkglib_LTLIBRARIES = select_bgq.la

# BGQ node selection plugin.
select_bgq_la_SOURCES = select_bgq.cc bgq.h bgq_enums.h
select_bgq_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(BG_LDFLAGS)

