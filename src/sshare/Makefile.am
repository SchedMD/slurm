# Makefile for sshare

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sshare

sshare_LDADD =  \
	$(top_builddir)/src/api/libslurm.o $(DL_LIBS) -lm \
	$(READLINE_LIBS)

sshare_SOURCES =	\
	process.c               \
	sshare.c sshare.h

sshare_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(sshare_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
