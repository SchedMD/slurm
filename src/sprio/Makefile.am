#
# Makefile for sprio

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sprio

sprio_LDADD = $(LIB_SLURM) $(DL_LIBS)
sprio_DEPENDENCIES = $(LIB_SLURM_BUILD)

noinst_HEADERS = sprio.h print.h
sprio_SOURCES = sprio.c print.c opts.c sort.c

force:
$(sprio_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sprio_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

