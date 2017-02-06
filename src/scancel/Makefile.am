# Makefile for scancel

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = scancel

scancel_LDADD = $(LIB_SLURM) $(DL_LIBS)
scancel_DEPENDENCIES = $(LIB_SLURM_BUILD)

noinst_HEADERS  = scancel.h
scancel_SOURCES = scancel.c opt.c

scancel_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(scancel_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
