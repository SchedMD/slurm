# Makefile for conmgr library

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

noinst_PROGRAMS = libconmgr.o
noinst_LTLIBRARIES = libconmgr.la

libconmgr_la_SOURCES = \
	conmgr.c \
	conmgr.h \
	workq.c \
	workq.h

libconmgr_la_LDFLAGS = $(LIB_LDFLAGS) -module --export-dynamic

# This was made so we could export all symbols from libconmgr
# on multiple platforms
libconmgr_o_SOURCES =
libconmgr.o$(EXEEXT) :  $(libconmgr_la_OBJECTS) $(libconmgr_la_DEPENDENCIES)
	$(LINK)  $(libconmgr_la_OBJECTS)
