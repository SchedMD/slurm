#

AUTOMAKE_OPTIONS = foreign
SRUN_SOURCES =		\
	allocate.c	\
	allocate.h	\
	debugger.h	\
	debugger.c	\
	fname.c		\
	fname.h		\
	multi_prog.c	\
	multi_prog.h	\
	opt.c		\
	opt.h		\
	srun.c		\
	srun.h		\
	srun_job.c	\
	srun_job.h	\
	srun_pty.c	\
	srun_pty.h	\
	srun.wrapper.c	\
	task_state.c	\
	task_state.h

EXTRA_srun_SOURCES =

CLEANFILES = core.*

INCLUDES = -I$(top_srcdir) $(BG_INCLUDES)

bin_PROGRAMS = srun

srun_SOURCES = $(SRUN_SOURCES)

# pthread is needed for compiling with g++ (which is used for linking
# no matter if on a BGQ or not because of mentioning a .cc file
convenience_libs = \
	$(top_builddir)/src/api/libslurm.o -ldl -lpthread

srun_LDADD = \
	$(convenience_libs)

srun_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

if BGQ_LOADED
srun_SOURCES += runjob_interface.cc
srun_LDFLAGS += $(RUNJOB_LDFLAGS)
else
EXTRA_srun_SOURCES += runjob_interface.cc
endif

if USE_LOADLEVELER
srun_SOURCES += load_leveler.c load_leveler.h
else
EXTRA_srun_SOURCES += load_leveler.c load_leveler.h
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
