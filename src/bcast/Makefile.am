# Makefile for bcast library

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

BCAST_LIB = libfile_bcast.la
libfile_bcast_la_SOURCES = file_bcast.c file_bcast.h
libfile_bcast_la_LIBADD  = $(ZLIB_LIBS) $(LZ4_LIBS)
libfile_bcast_la_LDFLAGS = $(LIB_LDFLAGS) $(ZLIB_LDFLAGS) $(LZ4_LDFLAGS)
libfile_bcast_la_CFLAGS  = $(ZLIB_CPPFLAGS) $(LZ4_CPPFLAGS) $(AM_CFLAGS)

noinst_LTLIBRARIES = $(BCAST_LIB)
