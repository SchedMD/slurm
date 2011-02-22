# Makefile for the bluegene sfree tool

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)  -I$(top_srcdir)/src/common $(BG_INCLUDES)

sbin_PROGRAMS = sfree

sfree_LDADD = $(top_builddir)/src/api/libslurm.o -ldl

sfree_SOURCES = sfree.c sfree.h opts.c
sfree_LDFLAGS = -export-dynamic -lm $(CMD_LDFLAGS)

force:
$(sfree_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
