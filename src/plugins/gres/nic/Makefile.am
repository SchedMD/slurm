# Makefile for gres/nic plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(HWLOC_CPPFLAGS)

if ! USE_LOADLEVELER

# Gres NIC plugin.
pkglib_LTLIBRARIES = gres_nic.la
gres_nic_la_SOURCES = gres_nic.c
gres_nic_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(HWLOC_LDFLAGS)
gres_nic_la_LIBADD  = $(HWLOC_LIBS)

else
EXTRA_gres_nic_la_SOURCES = gres_nic.c
endif
