# Makefile for sacct

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sacct

sacct_LDADD = $(LIB_SLURM)
sacct_DEPENDENCIES = $(LIB_SLURM_BUILD)

sacct_LDFLAGS = $(CMD_LDFLAGS)

noinst_HEADERS = sacct.h
sacct_SOURCES =		\
	options.c	\
	print.c		\
	process.c	\
	sacct.c		\
	sacct.h

force:
$(sacct_DEPENDENCIES) : force
	 @cd `dirname $@` && $(MAKE) `basename $@`
