#
# Makefile for sprio

AUTOMAKE_OPTIONS = foreign

SPRIO_SOURCES = print.c print.h sprio.c sprio.h opts.c

if ! USE_LOADLEVELER

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sprio

sprio_LDADD = 	$(top_builddir)/src/api/libslurm.o -ldl

sprio_SOURCES  = $(SPRIO_SOURCES)

force:
$(sprio_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sprio_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

else
EXTRA_sprio_SOURCES = $(SPRIO_SOURCES)
endif
