# Makefile for auth/jwt plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(JWT_CPPFLAGS)

pkglib_LTLIBRARIES = auth_jwt.la

# JWT authentication plugin.
auth_jwt_la_SOURCES = auth_jwt.c auth_jwt.h pem_key.c
auth_jwt_la_LDFLAGS = $(PLUGIN_FLAGS) $(JWT_LDFLAGS)
auth_jwt_la_LIBADD = $(JWT_LIBS)
