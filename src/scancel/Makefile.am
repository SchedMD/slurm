# Makefile for scancel

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = scancel

scancel_LDADD = $(top_builddir)/src/api/libslurm.o $(DL_LIBS)

noinst_HEADERS  = scancel.h
scancel_SOURCES = scancel.c opt.c

scancel_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(scancel_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
