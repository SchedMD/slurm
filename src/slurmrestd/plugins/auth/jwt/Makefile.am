# Makefile for auth/jwt plugin

AUTOMAKE_OPTIONS = foreign
CLEANFILES = *.bino

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -DSLURM_PLUGIN_DEBUG -I$(top_srcdir) -I$(top_srcdir)/src/common $(JSON_CPPFLAGS)

pkglib_LTLIBRARIES = rest_auth_jwt.la

rest_auth_jwt_la_SOURCES = jwt.c

rest_auth_jwt_la_LDFLAGS = $(PLUGIN_FLAGS)
