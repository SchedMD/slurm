# Makefile for jobacct/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

# Gold job completion logging plugin.
if HAVE_PERL_CORE_DIR
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common $(PERL_INCLUDES)

pkglib_LTLIBRARIES = jobacct_gold.la
jobacct_gold_la_SOURCES = jobacct_gold.c
jobacct_gold_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS) $(PERL_LIBS)

else
EXTRA_jobacct_gold_la_SOURCES = jobacct_gold.c
endif


