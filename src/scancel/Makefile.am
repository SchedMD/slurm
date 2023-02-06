# Makefile for scancel

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = scancel

scancel_LDADD = $(LIB_SLURM)
scancel_DEPENDENCIES = $(LIB_SLURM_BUILD)

noinst_HEADERS  = scancel.h
scancel_SOURCES = scancel.c opt.c

scancel_LDFLAGS = $(CMD_LDFLAGS)

force:
$(scancel_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
