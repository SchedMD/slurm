# $Id$
#

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) 

bin_PROGRAMS = srun

srun_SOURCES = \
	srun.c \
	opt.c opt.h \
	srun_job.c srun_job.h \
	msg.c msg.h \
	signals.c signals.h \
	io.c io.h   \
	launch.c    \
	launch.h    \
	attach.h    \
	attach.c    \
	reattach.c  \
	reattach.h  \
	fname.c     \
	fname.h     \
	sigstr.c    \
	sigstr.h    \
	allocate.c  \
	allocate.h  \
	core-format.c \
	core-format.h \
	srun.wrapper.c

convenience_libs =  \
	$(top_builddir)/src/common/libcommon.la   \
	$(top_builddir)/src/common/libeio.la      \
	$(top_builddir)/src/api/libslurm.la

srun_LDADD = \
	$(convenience_libs) 

srun_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(FEDERATION_LDFLAGS) $(SRUN_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

attach.o : attach.c
	$(COMPILE) -c -g -o attach.o $(srcdir)/attach.c 

install-exec-local:
	umask 022; \
	if [ -x /usr/lib/rpm/debugedit ]; then \
	  srcdir=`cd $(top_srcdir) && pwd`; \
	  /usr/lib/rpm/debugedit -b $$srcdir -d $(pkglibdir) \
	                         $(DESTDIR)$(bindir)/srun; fi; \
	$(INSTALL) -D -m 644 $(top_srcdir)/src/srun/srun.wrapper.c \
	                       $(DESTDIR)$(pkglibdir)/src/srun/srun.wrapper.c

