# Makefile for cred/munge plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(MUNGE_CPPFLAGS)

# Add your plugin to this line, following the naming conventions.
if WITH_MUNGE
MUNGE = cred_munge.la
endif

pkglib_LTLIBRARIES = $(MUNGE)

# Munge cred plugin
cred_munge_la_SOURCES = cred_munge.c
cred_munge_la_LDFLAGS = $(PLUGIN_FLAGS) $(MUNGE_LDFLAGS)
cred_munge_la_LIBADD =  $(MUNGE_LIBS)
