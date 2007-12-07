# Makefile for jobacct/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

if HAVE_PERL_CORE_DIR
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common -I$(PERL_CORE_DIR)
else
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common
endif

pkglib_LTLIBRARIES = jobacct_gold.la

# Gold job completion logging plugin.
jobacct_gold_la_SOURCES = jobacct_gold.c
jobacct_gold_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
