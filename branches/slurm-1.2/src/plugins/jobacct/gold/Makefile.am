# Makefile for jobacct/none plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

# Gold job completion logging plugin.
INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = jobacct_gold.la
jobacct_gold_la_SOURCES = jobacct_gold.c \
			gold_interface.c gold_interface.h \
			base64.c base64.h
jobacct_gold_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)


