# Makefile for PMI2 client side library.
#

AUTOMAKE_OPTIONS = foreign

PLUGIN_FLAGS = -module -avoid-version --export-dynamic

lib_LTLIBRARIES = libpmi2.la

libpmi2_la_SOURCES = pmi2_api.c  pmi2_util.c pmi2_api.h
libpmi2_la_LDFLAGS = $(SO_LDFLAGS) $(PLUGIN_FLAGS)

