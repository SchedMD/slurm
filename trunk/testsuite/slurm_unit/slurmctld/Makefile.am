AUTOMAKE_OPTIONS = foreign

noinst_PROGRAMS = job_mgr-test job_step-test job_add job_cancel step_create

INCLUDES = -I$(top_srcdir)/src/common

job_step_test_LDADD = $(top_srcdir)/src/common/libcommon.la \
		$(top_srcdir)/src/api/libslurm.la

step_create_LDADD = $(top_srcdir)/src/common/libcommon.la \
		$(top_srcdir)/src/api/libslurm.la

job_add_LDADD = $(top_srcdir)/src/common/libcommon.la \
		$(top_srcdir)/src/api/libslurm.la

job_cancel_LDADD = $(top_srcdir)/src/common/libcommon.la \
		$(top_srcdir)/src/api/libslurm.la

LDADD   = $(top_srcdir)/src/common/libcommon.la \
		$(top_srcdir)/src/slurmctld/job_scheduler.o \
		$(top_srcdir)/src/slurmctld/node_scheduler.o \
		$(top_srcdir)/src/slurmctld/partition_mgr.o \
		$(top_srcdir)/src/slurmctld/step_mgr.o \
		$(top_srcdir)/src/slurmctld/job_mgr.o \
		$(top_srcdir)/src/slurmctld/node_mgr.o \
		$(top_srcdir)/src/common/parse_spec.o  \
		$(top_srcdir)/src/slurmctld/read_config.o \
		$(top_srcdir)/src/api/libslurm.la

