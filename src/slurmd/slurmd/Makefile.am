#
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmd

AM_CPPFLAGS = -DLIBSLURM_SO=\"$(libdir)/libslurm.so\" \
	-I$(top_srcdir) $(HWLOC_CPPFLAGS)

depend_libs = $(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/bcast/libfile_bcast.la \
	../common/libslurmd_common.o ../common/libslurmd_reverse_tree_math.la

slurmd_LDADD = $(depend_libs) $(LIB_SLURM) $(HWLOC_LIBS) $(DL_LIBS)

slurmd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(HWLOC_LDFLAGS) $(NUMA_LIBS)

SLURMD_SOURCES = \
	slurmd.c slurmd.h \
	req.c req.h \
	get_mach_stat.c get_mach_stat.h

slurmd_SOURCES = $(SLURMD_SOURCES)

slurmd_DEPENDENCIES = $(depend_libs) $(LIB_SLURM_BUILD)

force:
$(slurmd_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
