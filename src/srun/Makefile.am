#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir) $(CRAY_JOB_CPPFLAGS)

bin_PROGRAMS = srun

depend_libs = $(top_builddir)/src/bcast/libfile_bcast.la

srun_SOURCES = \
	allocate.c allocate.h \
	debugger.c debugger.h \
	fname.c fname.h \
	launch.c launch.h \
	multi_prog.c multi_prog.h \
	opt.c opt.h \
	srun.c \
	srun_job.c srun_job.h \
	srun_pty.c srun_pty.h \
	srun.wrapper.c \
	step_ctx.c step_ctx.h \
	task_state.c task_state.h

srun_LDADD = $(depend_libs) $(LIB_SLURM)

srun_DEPENDENCIES = $(depend_libs) $(LIB_SLURM_BUILD)

srun_LDFLAGS = $(CMD_LDFLAGS) $(CRAY_JOB_LDFLAGS)

force:
$(srun_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

install-exec-local:
	umask 022; \
	if [ -x /usr/lib/rpm/debugedit ]; then \
	  srcdir=`cd $(top_srcdir) && pwd`; \
	  /usr/lib/rpm/debugedit -b $$srcdir -d $(pkglibdir) \
	                         $(DESTDIR)$(bindir)/srun; fi; \
	mkdir -p -m 755 $(DESTDIR)$(pkglibdir)/src/srun
	$(INSTALL) -m 644 $(top_srcdir)/src/srun/srun.wrapper.c \
		$(DESTDIR)$(pkglibdir)/src/srun/srun.wrapper.c

# debugging information is required for symbols in the debugger
# module so that a debugger can debugger to spawned tasks
debugger.o : debugger.c
	$(COMPILE) -c -g -o debugger.o $(srcdir)/debugger.c

