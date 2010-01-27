# Makefile for database library

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if WITH_MYSQL
MYSQL_LIB = libslurm_mysql.la
libslurm_mysql_la_SOURCES = mysql_common.c mysql_common.h
libslurm_mysql_la_LIBADD   = $(MYSQL_LIBS)
libslurm_mysql_la_LDFLAGS  = $(LIB_LDFLAGS)
libslurm_mysql_la_CFLAGS = $(MYSQL_CFLAGS)
else
MYSQL_LIB =
EXTRA_libslurm_mysql_la_SOURCES = mysql_common.c mysql_common.h
endif

if WITH_PGSQL
PGSQL_LIB = libslurm_pgsql.la
libslurm_pgsql_la_SOURCES = pgsql_common.c pgsql_common.h
libslurm_pgsql_la_LIBADD   = $(PGSQL_LIBS)
libslurm_pgsql_la_LDFLAGS  = $(LIB_LDFLAGS)
libslurm_pgsql_la_CFLAGS = $(PGSQL_CFLAGS)
else
PGSQL_LIB =
EXTRA_libslurm_pgsql_la_SOURCES = pgsql_common.c pgsql_common.h
endif

noinst_LTLIBRARIES = $(MYSQL_LIB) $(PGSQL_LIB)
