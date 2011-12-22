#
# Makefile for sbcast

AUTOMAKE_OPTIONS = foreign

SBCAST_SOURCES =	\
	agent.c		\
	sbcast.c	\
	sbcast.h	\
	opts.c

if ! USE_LOADLEVELER

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

bin_PROGRAMS = sbcast

sbcast_LDADD = 	$(top_builddir)/src/api/libslurm.o -ldl -lm

sbcast_SOURCES = $(SBCAST_SOURCES)

force:
$(sbcast_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sbcast_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

else
EXTRA_sbcast_SOURCES = $(SBCAST_SOURCES)
endif
