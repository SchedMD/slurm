#
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

CPPFLAGS = -DLIBSLURM_SO=\"$(libdir)/libslurm.so\"
INCLUDES = -I$(top_srcdir)

SLURMD_SOURCES =			\
	get_mach_stat.c get_mach_stat.h	\
	read_proc.c 	        	\
	req.c req.h			\
	reverse_tree_math.c		\
	reverse_tree_math.h		\
	slurmd.c slurmd.h		\
	xcpu.c xcpu.h

if ! USE_LOADLEVELER

sbin_PROGRAMS = slurmd

slurmd_LDADD = 					   \
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/api/libslurm.o -ldl	   \
	$(PLPA_LIBS) \
	../common/libslurmd_common.la

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

else
EXTRA_slurmd_SOURCES = $(SLURMD_SOURCES)
endif
