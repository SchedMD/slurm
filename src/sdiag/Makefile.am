#
# Makefile for sdiag

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)
bin_PROGRAMS = sdiag

sdiag_LDADD = $(LIB_SLURM)
sdiag_DEPENDENCIES = $(LIB_SLURM_BUILD)

sdiag_SOURCES = sdiag.c opts.c

force:
$(sdiag_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sdiag_LDFLAGS = $(CMD_LDFLAGS)

