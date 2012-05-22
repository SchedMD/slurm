#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = srun

srun_SOURCES = \
	srun.c \
	opt.c opt.h \
	srun_job.c srun_job.h \
	srun_pty.c srun_pty.h \
	debugger.h    \
	debugger.c    \
	fname.c     \
	fname.h     \
	allocate.c  \
	allocate.h  \
	launch.c launch.h \
	multi_prog.c multi_prog.h \
	srun.wrapper.c


# pthread is needed for compiling with g++ (which is used for linking
# no matter if on a BGQ or not because of mentioning a .cc file
convenience_libs = \
	$(top_builddir)/src/api/libslurm.o -lpthread $(DL_LIBS)

srun_LDADD = \
	$(convenience_libs)

srun_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

if BGQ_LOADED
# because totalview needs the libraries on load we link in srun
# and not in the launch plugin.
srun_LDFLAGS += $(RUNJOB_LDFLAGS)
endif

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

# debugging information is required for symbols in the debugger
# module so that a debugger can debugger to spawned tasks
debugger.o : debugger.c
	$(COMPILE) -c -g -o debugger.o $(srcdir)/debugger.c

install-exec-local:
	umask 022; \
	if [ -x /usr/lib/rpm/debugedit ]; then \
	  srcdir=`cd $(top_srcdir) && pwd`; \
	  /usr/lib/rpm/debugedit -b $$srcdir -d $(pkglibdir) \
	                         $(DESTDIR)$(bindir)/srun; fi; \
	mkdir -p -m 755 $(DESTDIR)$(pkglibdir)/src/srun
	$(INSTALL) -m 644 $(top_srcdir)/src/srun/srun.wrapper.c \
		$(DESTDIR)$(pkglibdir)/src/srun/srun.wrapper.c

