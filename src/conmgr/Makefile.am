# Makefile for conmgr library

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

noinst_PROGRAMS = libconmgr.o
noinst_LTLIBRARIES = libconmgr.la

libconmgr_la_SOURCES = \
	con.c \
	conmgr.c \
	conmgr.h \
	delayed.c \
	delayed.h \
	events.c \
	events.h \
	io.c \
	mgr.h \
	poll.c \
	polling.c \
	polling.h \
	rpc.c \
	signals.c \
	signals.h \
	watch.c \
	work.c \
	workers.c

if HAVE_EPOLL
libconmgr_la_SOURCES += epoll.c
endif

libconmgr_la_LDFLAGS = $(LIB_LDFLAGS) -module --export-dynamic

# This was made so we could export all symbols from libconmgr
# on multiple platforms
libconmgr_o_SOURCES =
libconmgr.o$(EXEEXT) :  $(libconmgr_la_OBJECTS) $(libconmgr_la_DEPENDENCIES)
	$(LINK)  $(libconmgr_la_OBJECTS)
