# Makefile for building Cray/Basil XML-RPC low-level interface

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common -I. -I../

noinst_LTLIBRARIES = libalps.la

libalps_la_SOURCES =		\
	../basil_alps.h		\
	alps_emulate.c		\
	hilbert.c		\
	hilbert.h
libalps_la_CFLAGS  = $(MYSQL_CFLAGS)
libalps_la_LIBADD  = $(MYSQL_LIBS) -lexpat
libalps_la_LDFLAGS = $(LIB_LDFLAGS) -module --export-dynamic
