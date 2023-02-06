#
# Makefile for sprio

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sprio

sprio_LDADD = $(LIB_SLURM)
sprio_DEPENDENCIES = $(LIB_SLURM_BUILD)

noinst_HEADERS = sprio.h print.h
sprio_SOURCES = sprio.c print.c opts.c sort.c filter.c

force:
$(sprio_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sprio_LDFLAGS = $(CMD_LDFLAGS)

