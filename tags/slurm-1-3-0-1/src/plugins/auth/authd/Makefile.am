# Makefile for auth/authd plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic 

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

# Add your plugin to this line, following the naming conventions.
if WITH_AUTHD
AUTHD = auth_authd.la
endif

pkglib_LTLIBRARIES = $(AUTHD)

# Authd authentication plugin -- requires libauth
auth_authd_la_SOURCES = auth_authd.c
auth_authd_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
auth_authd_la_LIBADD = -lauth -le

