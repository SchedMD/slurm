AUTOMAKE_OPTIONS = foreign

noinst_PROGRAMS = job_mgr-test

INCLUDES = -I$(top_srcdir)/src/common
LDADD   = $(top_srcdir)/src/common/libcommon.la \
		$(top_srcdir)/src/slurmctld/job_scheduler.o \
		$(top_srcdir)/src/slurmctld/node_scheduler.o \
		$(top_srcdir)/src/slurmctld/partition_mgr.o \
		$(top_srcdir)/src/slurmctld/step_mgr.o \
		$(top_srcdir)/src/slurmctld/job_mgr.o \
		$(top_srcdir)/src/slurmctld/node_mgr.o \
		$(top_srcdir)/src/slurmctld/parse_spec.o  \
		$(top_srcdir)/src/slurmctld/read_config.o \
		$(top_srcdir)/src/api/libslurm.la
