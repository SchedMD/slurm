#
# Makefile for sacct

AUTOMAKE_OPTIONS = foreign

SACCT_SOURCES =		\
	options.c	\
	print.c		\
	process.c	\
	sacct.c		\
	sacct.h

if ! USE_LOADLEVELER

CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sacct

sacct_LDADD = 	$(top_builddir)/src/db_api/libslurmdb.o -ldl

sacct_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

sacct_SOURCES  = $(SACCT_SOURCES)

force:
$(sacct_LDADD) : force
	 @cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_sacct_SOURCES  = $(SACCT_SOURCES)
endif
