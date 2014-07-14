# Makefile for sstat

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sstat

sstat_LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

sstat_SOURCES =		\
	options.c	\
	print.c		\
	process.c	\
	sstat.c		\
	sstat.h

force:
$(sstat_LDADD) : force
	 @cd `dirname $@` && $(MAKE) `basename $@`

sstat_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

