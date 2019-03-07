#
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmd

depend_libs = $(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/bcast/libfile_bcast.la \
	../common/libslurmd_common.o ../common/libslurmd_reverse_tree_math.la

# These 2 depend_* should look the same in ../slurmstepd/Makefile.am
depend_ldadd = $(HWLOC_LIBS) $(PAM_LIBS) $(UTIL_LIBS)
depend_ldflags = $(HWLOC_LDFLAGS)

# This is needed to actually link to the libs that it doesn't use.
depend_ldflags += -Wl,--no-as-needed

AM_CPPFLAGS = -I$(top_srcdir) $(HWLOC_CPPFLAGS)

slurmd_LDADD = $(depend_libs) $(LIB_SLURM) $(DL_LIBS) $(NUMA_LIBS) \
	$(depend_ldadd)

slurmd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(depend_ldflags)

SLURMD_SOURCES = \
	slurmd.c slurmd.h \
	req.c req.h \
	get_mach_stat.c get_mach_stat.h

slurmd_SOURCES = $(SLURMD_SOURCES)

slurmd_DEPENDENCIES = $(depend_libs) $(LIB_SLURM_BUILD)

force:
$(slurmd_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
