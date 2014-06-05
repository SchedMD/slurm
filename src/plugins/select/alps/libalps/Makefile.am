# Makefile for building Cray/Basil XML-RPC low-level interface

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common -I. -I../

noinst_LTLIBRARIES = libalps.la

libalps_la_SOURCES =		\
	../basil_alps.h		\
	parser_internal.h	\
	basil_mysql_routines.c	\
	parser_common.c		\
	parser_basil_1.0.c	\
	parser_basil_1.1.c	\
	parser_basil_3.1.c	\
	parser_basil_4.0.c	\
	parser_basil_5.1.c	\
	parser_basil_5.2.c	\
	basil_request.c		\
	do_query.c		\
	do_reserve.c		\
	do_release.c		\
	do_confirm.c		\
	do_switch.c		\
	memory_handling.c	\
	memory_handling.h	\
	popen2.c		\
	atoul.c
libalps_la_CFLAGS  = $(MYSQL_CFLAGS)
libalps_la_LIBADD  = $(MYSQL_LIBS) -lexpat
libalps_la_LDFLAGS = $(LIB_LDFLAGS) -module --export-dynamic
