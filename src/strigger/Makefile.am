#
# Makefile for strigger

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)
bin_PROGRAMS = strigger

strigger_LDADD = $(LIB_SLURM)
strigger_DEPENDENCIES = $(LIB_SLURM_BUILD)

noinst_HEADERS = strigger.h
strigger_SOURCES = strigger.c opts.c

force:
$(strigger_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

strigger_LDFLAGS = $(CMD_LDFLAGS)

