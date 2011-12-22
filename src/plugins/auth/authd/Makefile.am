# Makefile for auth/authd plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if BUILD_AUTHD

# Authd authentication plugin -- requires libauth
pkglib_LTLIBRARIES = auth_authd.la
auth_authd_la_SOURCES = auth_authd.c
auth_authd_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)
auth_authd_la_LIBADD = -lauth -le

else
EXTRA_auth_authd_la_SOURCES = auth_authd.c
endif
