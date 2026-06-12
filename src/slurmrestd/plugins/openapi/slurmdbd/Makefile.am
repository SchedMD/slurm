# Makefile for openapi/slurmdbd plugin

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

AM_CPPFLAGS = -I$(top_srcdir) \
	      -I$(top_srcdir)/src/slurmrestd/plugins/openapi/slurmdbd/

pkglib_LTLIBRARIES = openapi_slurmdbd.la

openapi_slurmdbd_la_SOURCES = \
	accounts.c associations.c api.h api.c cluster.c \
	config.c diag.c instances.c jobs.c qos.c \
	tres.c users.c wckeys.c

openapi_slurmdbd_la_LDFLAGS = $(PLUGIN_FLAGS)
