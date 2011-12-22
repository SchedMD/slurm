# Makefile for sshare

AUTOMAKE_OPTIONS = foreign

SSHARE_SOURCES =	\
	process.c	\
	sshare.c	\
	sshare.h

if ! USE_LOADLEVELER

CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sshare

sshare_LDADD =  \
	$(top_builddir)/src/api/libslurm.o -ldl -lm \
	$(READLINE_LIBS)

sshare_SOURCES = $(SSHARE_SOURCES)

sshare_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(sshare_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_sshare_SOURCES = $(SSHARE_SOURCES)
endif
