#
# Makefile for sh5util_old lib, can be taken out 2 versions past 15.08
#

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common -I.

noinst_LTLIBRARIES = libsh5util_old.la

libsh5util_old_la_SOURCES = sh5util.c sh5util_old.h hdf5_api.c hdf5_api.h

libsh5util_old_la_LDFLAGS = $(LIB_LDFLAGS) -module --export-dynamic
