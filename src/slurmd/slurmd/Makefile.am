#
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmd

CPPFLAGS =  -DLIBSLURM_SO=\"$(libdir)/libslurm.so\"
INCLUDES = -I$(top_srcdir)

slurmd_LDADD = 					   \
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/api/libslurm.o -ldl	   \
	$(PLPA_LIBS)

SLURMD_SOURCES = \
	slurmd.c slurmd.h \
	req.c req.h \
	get_mach_stat.c get_mach_stat.h	\
	read_proc.c 	        	\
	reverse_tree_math.c reverse_tree_math.h \
	xcpu.c xcpu.h			\
	$(top_builddir)/src/slurmd/common/proctrack.c \
	$(top_builddir)/src/slurmd/common/proctrack.h \
	$(top_builddir)/src/slurmd/common/setproctitle.c \
	$(top_builddir)/src/slurmd/common/setproctitle.h \
	$(top_builddir)/src/slurmd/common/slurmstepd_init.c \
	$(top_builddir)/src/slurmd/common/slurmstepd_init.h \
	$(top_builddir)/src/slurmd/common/run_script.c \
	$(top_builddir)/src/slurmd/common/run_script.h \
	$(top_builddir)/src/slurmd/common/task_plugin.c \
	$(top_builddir)/src/slurmd/common/task_plugin.h \
	$(top_builddir)/src/slurmd/common/set_oomadj.c \
	$(top_builddir)/src/slurmd/common/set_oomadj.h \
	$(top_builddir)/src/slurmd/common/reverse_tree.h

slurmd_SOURCES = $(SLURMD_SOURCES)

if HAVE_AIX
# We need to set maxdata back to 0 because this effects the "max memory size"
# ulimit, and the ulimit is inherited by child processes.
slurmd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) -Wl,-bmaxdata:0x0
else
slurmd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)
endif

force:
$(slurmd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
