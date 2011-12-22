# Makefile for auth/munge plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(MUNGE_CPPFLAGS)

if BUILD_MUNGE

# Munge authentication plugin
pkglib_LTLIBRARIES = auth_munge.la
auth_munge_la_SOURCES = auth_munge.c
auth_munge_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(MUNGE_LDFLAGS)
auth_munge_la_LIBADD =  $(MUNGE_LIBS)

else
EXTRA_auth_munge_la_SOURCES = auth_munge.c
endif
