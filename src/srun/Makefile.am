#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) 

bin_PROGRAMS = srun

srun_SOURCES = \
	srun.c srun.h \
	opt.c opt.h \
	srun_job.c srun_job.h \
	srun_pty.c srun_pty.h \
	debugger.h    \
	debugger.c    \
	fname.c     \
	fname.h     \
	allocate.c  \
	allocate.h  \
	core-format.c \
	core-format.h \
	multi_prog.c multi_prog.h \
	srun.wrapper.c

convenience_libs = \
	$(top_builddir)/src/api/libslurm.o -ldl

srun_LDADD = \
	$(convenience_libs) 

srun_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

# debugging information is required for symbols in the attach 
# module so that a debugger can attach to spawned tasks
attach.o : attach.c
	$(COMPILE) -c -g -o attach.o $(srcdir)/attach.c 

install-exec-local:
	umask 022; \
	if [ -x /usr/lib/rpm/debugedit ]; then \
	  srcdir=`cd $(top_srcdir) && pwd`; \
	  /usr/lib/rpm/debugedit -b $$srcdir -d $(pkglibdir) \
	                         $(DESTDIR)$(bindir)/srun; fi; \
	mkdir -p -m 755 $(DESTDIR)$(pkglibdir)/src/srun
	$(INSTALL) -m 644 $(top_srcdir)/src/srun/srun.wrapper.c \
		$(DESTDIR)$(pkglibdir)/src/srun/srun.wrapper.c

