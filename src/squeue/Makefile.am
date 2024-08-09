#
# Makefile for squeue

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = squeue

squeue_LDADD = 	$(LIB_SLURM)
squeue_DEPENDENCIES = $(LIB_SLURM_BUILD)

noinst_HEADERS = squeue.h print.h
squeue_SOURCES = squeue.c print.c opts.c sort.c
squeue_LDFLAGS = $(CMD_LDFLAGS)

REF = help.txt usage.txt
include $(top_srcdir)/make_ref.include
noinst_LTLIBRARIES = $(LIB_REF)
EXTRA_squeue_DEPENDENCIES = $(LIB_REF)
squeue_LDADD += $(LIB_REF)

force:
$(squeue_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
