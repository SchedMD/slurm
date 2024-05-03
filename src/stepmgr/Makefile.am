# Makefile for stepmgr library

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

noinst_PROGRAMS = libstepmgr.o
noinst_LTLIBRARIES = libstepmgr.la

libstepmgr_la_SOURCES = \
	step_mgr.h \
	step_mgr.c
libstepmgr_o_SOURCES =
libstepmgr.o$(EXEEXT) :  $(libstepmgr_la_OBJECTS) $(libstepmgr_la_DEPENDENCIES)
	$(LINK)  $(libstepmgr_la_OBJECTS)
