#

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir) -I$(top_srcdir)/src/srun/libsrun

noinst_LTLIBRARIES = libsrun.la

libsrun_la_SOURCES = \
	allocate.c allocate.h \
	debugger.c debugger.h \
	fname.c fname.h \
	launch.c launch.h \
	multi_prog.c multi_prog.h \
	opt.c opt.h \
	srun_job.c srun_job.h
libsrun_la_LIBADD = $(DL_LIBS)
libsrun_la_LDFLAGS  = $(LIB_LDFLAGS) -module --export-dynamic

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

# debugging information is required for symbols in the debugger
# module so that a debugger can debugger to spawned tasks
debugger.o : debugger.c
	$(COMPILE) -c -g -o debugger.o $(srcdir)/debugger.c

