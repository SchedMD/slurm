# Makefile for checkpoint/poe plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

# POE checkpoint plugin.
if DONT_BUILD
# if HAVE_NRT
pkglib_LTLIBRARIES = checkpoint_poe.la
checkpoint_poe_la_SOURCES = checkpoint_poe.c
checkpoint_poe_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
else
EXTRA_checkpoint_poe_la_SOURCES =  checkpoint_poe.c
endif
