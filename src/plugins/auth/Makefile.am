# $Id$
# Makefile for auth plugins

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

# Add your plugin to this line, following the naming conventions.
lib_LTLIBRARIES = libauth_none.la libauth_authd.la

# Null authentication plugin.
libauth_none_la_SOURCES = auth_none.c
libauth_none_la_LDFLAGS = $(PLUGIN_FLAGS)
libauth_none_la_LIBADD = -lslurm

# Authd authentication plugin -- requires libauth
libauth_authd_la_SOURCES = auth_authd.c
libauth_authd_la_LDFLAGS = $(PLUGIN_FLAGS)
libauth_authd_la_LIBADD = -lslurm -lauth

