# Makefile for cred/munge plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) $(MUNGE_CPPFLAGS)

pkglib_LTLIBRARIES = cred_munge.la

# Munge cred plugin
cred_munge_la_SOURCES = cred_munge.c
cred_munge_la_LDFLAGS = $(PLUGIN_FLAGS) $(MUNGE_LDFLAGS)
cred_munge_la_LIBADD =  $(MUNGE_LIBS) ../common/libcred_common.la
