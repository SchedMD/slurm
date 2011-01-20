# Makefile.am for slurmd/common

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

# making a .la

noinst_LTLIBRARIES = libslurmd_common.la
libslurmd_common_la_SOURCES =    \
	proctrack.c proctrack.h \
	setproctitle.c setproctitle.h \
	slurmstepd_init.c slurmstepd_init.h \
	run_script.c run_script.h \
	task_plugin.c task_plugin.h \
	set_oomadj.c set_oomadj.h \
	reverse_tree.h
