# Makefile.am for slurmd/common

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

LIBSLURMD_COMMON_LA_SOURCES =		\
	proctrack.c proctrack.h		\
	reverse_tree.h			\
	run_script.c run_script.h	\
	set_oomadj.c set_oomadj.h	\
	setproctitle.c setproctitle.h	\
	slurmstepd_init.c		\
	slurmstepd_init.h		\
	task_plugin.c task_plugin.h

if ! USE_LOADLEVELER
# making a .la

noinst_LTLIBRARIES = libslurmd_common.la
libslurmd_common_la_SOURCES = $(LIBSLURMD_COMMON_LA_SOURCES)

else
EXTRA_libslurmd_common_la_SOURCES = $(LIBSLURMD_COMMON_LA_SOURCES)
endif
