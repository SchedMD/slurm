# $Id$
#

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) 

if WITH_TOTALVIEW
SRUN_WRAPPER_OBJ = srun.wrapper.o
endif


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
	allocate.h

convenience_libs =  \
	$(top_builddir)/src/common/libcommon.la   \
	$(top_builddir)/src/common/libhostlist.la \
	$(top_builddir)/src/common/libeio.la      \
	$(top_builddir)/src/api/libslurm.la

srun_LDADD = \
	$(SRUN_WRAPPER_OBJ) \
	$(convenience_libs)

EXTRA_srun_SOURCES = srun.wrapper.c
srun_DEPENDENCIES = $(SRUN_WRAPPER_OBJ)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

srun.wrapper.o: srun.wrapper.c
	if echo $(CC) | grep gcc; then \
          $(CC) -S -g $(CFLAGS) $(CPPFLAGS) \
                $(top_srcdir)/src/srun/srun.wrapper.c; \
          dir1=`cd $(top_srcdir) && pwd`;  dir2=$(pkglibdir)/src; \
	  perl -i -ple "s|$$dir1/src/srun|$$dir2|g;" srun.wrapper.s; \
	  perl -i -ple "s|\".*/srun.wrapper.c|\"srun.wrapper.c|g;" \
                       srun.wrapper.s; \
	  $(CC) -c -g $(CFLAGS) $(CPPFLAGS) srun.wrapper.s; \
	else \
	  $(COMPILE) srun.wrapper.c; \
	fi

CLEANFILES = \
	srun.wrapper.s

install-exec-local:
	if test -n "$(SRUN_WRAPPER_OBJ)"; then \
	  umask 022; \
	  $(INSTALL) -D -m 644 $(top_srcdir)/src/srun/srun.wrapper.c \
	                       $(DESTDIR)$(pkglibdir)/src/srun.wrapper.c; \
	fi
