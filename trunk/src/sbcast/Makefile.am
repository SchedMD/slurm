#
# Makefile for sbcast

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)
bin_PROGRAMS = sbcast

sbcast_LDADD = 	$(top_builddir)/src/api/libslurm.o -ldl

noinst_HEADERS = sbcast.h
sbcast_SOURCES = agent.c sbcast.c opts.c

force:
$(sbcast_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

sbcast_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

