# Makefile for mcs/user plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common

pkglib_LTLIBRARIES = mcs_user.la

# mcs/user plugin.
mcs_user_la_SOURCES = mcs_user.c
mcs_user_la_LDFLAGS = $(PLUGIN_FLAGS)
