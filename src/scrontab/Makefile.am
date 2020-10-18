# Makefile for scrontab

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = scrontab

scrontab_SOURCES =	\
	scrontab.c	\
	scrontab.h

scrontab_LDADD = $(LIB_SLURM) $(DL_LIBS)
scrontab_DEPENDENCIES = $(LIB_SLURM_BUILD)

scrontab_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(scrontab_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
