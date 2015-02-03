# Makefile.am for slurmd/common

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir) $(HWLOC_CPPFLAGS)

# making a .la

noinst_LTLIBRARIES = libslurmd_common.la libslurmd_reverse_tree_math.la
noinst_PROGRAMS = libslurmd_common.o

libslurmd_common_la_SOURCES =    \
	core_spec_plugin.c core_spec_plugin.h \
	job_container_plugin.c job_container_plugin.h \
	proctrack.c proctrack.h \
	setproctitle.c setproctitle.h \
	slurmd_cgroup.c slurmd_cgroup.h \
	slurmstepd_init.c slurmstepd_init.h \
	run_script.c run_script.h \
	task_plugin.c task_plugin.h \
	set_oomadj.c set_oomadj.h \
	reverse_tree.h \
	xcpuinfo.c xcpuinfo.h \
	xcgroup.c xcgroup.h


libslurmd_common_la_LDFLAGS  = $(HWLOC_LDFLAGS) $(HWLOC_LIBS)

# This was made so we could export all symbols from libslurm_common
# Since the plugins loaded may need all the symbols here.
libslurmd_common_o_SOURCES =
libslurmd_common.o$(EXEEXT) : \
	$(libslurmd_common_la_OBJECTS) $(libslurmd_common_la_DEPENDENCIES)
	$(LINK)  $(libslurmd_common_la_OBJECTS)

libslurmd_reverse_tree_math_la_SOURCES = \
	reverse_tree_math.c \
	reverse_tree_math.h
