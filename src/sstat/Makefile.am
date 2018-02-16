# Makefile for sstat

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sstat

sstat_LDADD = $(LIB_SLURMDB) $(DL_LIBS)
sstat_DEPENDENCIES = $(LIB_SLURM_BUILD) $(LIB_SLURMDB_BUILD)

sstat_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

noinst_HEADERS = sstat.h

sstat_SOURCES =		\
	options.c	\
	print.c		\
	sstat.c		\
	sstat.h

force:
$(sstat_LDADD) : force
	 @cd `dirname $@` && $(MAKE) `basename $@`
