# Makefile for sstat

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sstat

sstat_LDADD = $(LIB_SLURM) $(DL_LIBS)
sstat_DEPENDENCIES = $(LIB_SLURM_BUILD)

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

