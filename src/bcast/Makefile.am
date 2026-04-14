# Makefile for bcast library

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

BCAST_LIB = libfile_bcast.la
libfile_bcast_la_SOURCES = file_bcast.c file_bcast.h
libfile_bcast_la_LDFLAGS = $(LIB_LDFLAGS)
libfile_bcast_la_CFLAGS  = $(AM_CFLAGS)

noinst_LTLIBRARIES = $(BCAST_LIB)
