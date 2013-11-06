# Makefile for database library

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/common

if WITH_MYSQL
MYSQL_LIB = libslurm_mysql.la
libslurm_mysql_la_SOURCES = mysql_common.c mysql_common.h
libslurm_mysql_la_LIBADD   = $(MYSQL_LIBS)
libslurm_mysql_la_LDFLAGS  = $(LIB_LDFLAGS)
libslurm_mysql_la_CFLAGS = $(MYSQL_CFLAGS) $(AM_CFLAGS)
else
MYSQL_LIB =
EXTRA_libslurm_mysql_la_SOURCES = mysql_common.c mysql_common.h
endif

noinst_LTLIBRARIES = $(MYSQL_LIB)
