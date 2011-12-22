#
# Makefile for strigger

AUTOMAKE_OPTIONS = foreign

STRIGGER_SOURCES = strigger.c strigger.h opts.c

if ! USE_LOADLEVELER

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

bin_PROGRAMS = strigger

strigger_LDADD = $(top_builddir)/src/api/libslurm.o -ldl

strigger_SOURCES = $(STRIGGER_SOURCES)

force:
$(strigger_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

strigger_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

else
EXTRA_strigger_SOURCES = $(STRIGGER_SOURCES)
endif
