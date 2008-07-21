# Makefile for database library 

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

if HAVE_OPENSSL

noinst_LTLIBRARIES =      \
	libslurm_mysql.la \
	libslurm_pgsql.la \
	libslurm_gold.la

libslurm_gold_la_SOURCES = gold_interface.c gold_interface.h \
	base64.c base64.h
libslurm_gold_la_LIBADD   = $(SSL_LIBS)
libslurm_gold_la_LDFLAGS  = $(LIB_LDFLAGS) $(SSL_LDFLAGS)
libslurm_gold_la_CFLAGS   = $(SSL_CPPFLAGS)

else 

noinst_LTLIBRARIES =      \
	libslurm_mysql.la \
	libslurm_pgsql.la

endif

libslurm_mysql_la_SOURCES = mysql_common.c mysql_common.h
libslurm_pgsql_la_SOURCES = pgsql_common.c pgsql_common.h

libslurm_mysql_la_LIBADD   = $(MYSQL_LIBS) 
libslurm_pgsql_la_LIBADD   = $(PGSQL_LIBS) 

libslurm_mysql_la_LDFLAGS  = $(LIB_LDFLAGS)
libslurm_pgsql_la_LDFLAGS  = $(LIB_LDFLAGS)

libslurm_mysql_la_CFLAGS = $(MYSQL_CFLAGS)
libslurm_pgsql_la_CFLAGS = $(PGSQL_CFLAGS)
