# Makefile for sshare

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sshare

sshare_LDADD = $(LIB_SLURM) $(READLINE_LIBS)
sshare_DEPENDENCIES = $(LIB_SLURM_BUILD)

sshare_SOURCES =	\
	process.c               \
	sshare.c sshare.h

sshare_LDFLAGS = $(CMD_LDFLAGS)

force:
$(sshare_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
