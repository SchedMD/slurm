#
# Makefile for strigger

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)
bin_PROGRAMS = strigger

strigger_LDADD = $(LIB_SLURM) $(DL_LIBS)
strigger_DEPENDENCIES = $(LIB_SLURM_BUILD)

noinst_HEADERS = strigger.h
strigger_SOURCES = strigger.c opts.c

force:
$(strigger_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

strigger_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

