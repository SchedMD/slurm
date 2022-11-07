# Makefile.am for slurmd/common

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir) $(HWLOC_CPPFLAGS)

# making a .la

noinst_LTLIBRARIES = libslurmd_common.la
noinst_PROGRAMS = libslurmd_common.o

libslurmd_common_la_SOURCES =    \
	fname.c fname.h \
	slurmd_cgroup.c slurmd_cgroup.h \
	slurmstepd_init.c slurmstepd_init.h \
	set_oomadj.c set_oomadj.h \
	xcpuinfo.c xcpuinfo.h

libslurmd_common_la_LDFLAGS  = $(HWLOC_LDFLAGS) $(HWLOC_LIBS)

# This was made so we could export all symbols from libslurm_common
# Since the plugins loaded may need all the symbols here.
libslurmd_common_o_SOURCES =
libslurmd_common.o$(EXEEXT) : \
	$(libslurmd_common_la_OBJECTS) $(libslurmd_common_la_DEPENDENCIES)
	$(LINK)  $(libslurmd_common_la_OBJECTS)

force:
$(libslurmd_common_la_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
