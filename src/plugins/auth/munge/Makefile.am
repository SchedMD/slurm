# Makefile for auth/munge plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common $(MUNGE_CPPFLAGS)

# Add your plugin to this line, following the naming conventions.
if WITH_MUNGE
MUNGE = auth_munge.la
endif

pkglib_LTLIBRARIES = $(MUNGE)

# Munge authentication plugin
auth_munge_la_SOURCES = auth_munge.c
auth_munge_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(MUNGE_LDFLAGS)
auth_munge_la_LIBADD =  $(MUNGE_LIBS)
