#
# Makefile for sbcast

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)
bin_PROGRAMS = sbcast

sbcast_LDADD = 	$(LIB_SLURM)
sbcast_DEPENDENCIES = $(LIB_SLURM_BUILD)

noinst_HEADERS = sbcast.h
sbcast_SOURCES = sbcast.c opts.c

force:
$(sbcast_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sbcast_LDFLAGS = $(CMD_LDFLAGS)
