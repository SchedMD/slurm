# Makefile for stepmgr library

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

noinst_PROGRAMS = libstepmgr.o
noinst_LTLIBRARIES = libstepmgr.la

libstepmgr_la_SOURCES = \
	gres_stepmgr.c \
	gres_stepmgr.h \
	srun_comm.c \
	srun_comm.h \
	stepmgr.h \
	stepmgr.c
libstepmgr_o_SOURCES =
libstepmgr.o$(EXEEXT) :  $(libstepmgr_la_OBJECTS) $(libstepmgr_la_DEPENDENCIES)
	$(LINK)  $(libstepmgr_la_OBJECTS)
