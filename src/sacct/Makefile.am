# Makefile for sacct

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sacct

sacct_LDADD = 	$(top_builddir)/src/db_api/libslurmdb.o -ldl

sacct_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

noinst_HEADERS = sacct.c
sacct_SOURCES =		\
	options.c	\
	print.c		\
	process.c	\
	sacct.c		\
	sacct.h

force:
$(sacct_LDADD) : force
	 @cd `dirname $@` && $(MAKE) `basename $@`


