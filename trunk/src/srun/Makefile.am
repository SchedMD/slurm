# $Id$
#

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) 

bin_PROGRAMS = srun

srun_SOURCES = \
	srun.c \
	opt.c opt.h \
	env.c env.h \
	job.c job.h \
	net.c net.h \
	msg.c msg.h \
	io.c io.h   \
	launch.c    \
	launch.h    \
	attach.h    \
	reattach.c  \
	reattach.h  \
	fname.c     \
	fname.h     \
	signals.c   \
	signals.h   \
	sigstr.c    \
	sigstr.h    \
	allocate.c  \
	allocate.h  \
	core-format.c \
	core-format.h

convenience_libs =  \
	$(top_builddir)/src/common/libcommon.la   \
	$(top_builddir)/src/common/libeio.la      \
	$(top_builddir)/src/api/libslurm.la

SRUN_WRAPPER_OBJ = \
	srun.wrapper.o

srun_LDADD = \
	$(SRUN_WRAPPER_OBJ) \
	$(convenience_libs)

srun_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

EXTRA_srun_SOURCES = srun.wrapper.c
srun_DEPENDENCIES = $(SRUN_WRAPPER_OBJ)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

srun.wrapper.o: srun.wrapper.c
	if echo $(CC) | grep gcc; then \
          destdir=$(pkglibdir)/src;    \
          cdir=`cd $(top_srcdir)/src/srun && pwd`; \
          src=$$cdir/srun.wrapper.c;   \
          cp $$src .; \
          $(CC) -S -g $(CFLAGS) $(CPPFLAGS) $$src; \
	  perl -i -ple "s|$$cdir|$$destdir|g;" srun.wrapper.s; \
          $(COMPILE) -c srun.wrapper.s; \
	else \
	  $(COMPILE) -c $(top_srcdir)/src/srun/srun.wrapper.c; \
	fi


CLEANFILES = \
	srun.wrapper.s

DISTCLEANFILES = \
	srun.wrapper.c

install-exec-local:
	if test -n "$(SRUN_WRAPPER_OBJ)"; then \
	  umask 022; \
	  $(INSTALL) -D -m 644 $(top_srcdir)/src/srun/srun.wrapper.c \
	                       $(DESTDIR)$(pkglibdir)/src/srun.wrapper.c; \
	fi
