AUTOMAKE_OPTIONS = foreign

noinst_PROGRAMS = job_mgr-test job_step-test job_add job_cancel step_create
noinst_HEADERS  = get_resp.h

INCLUDES = -I$(top_srcdir) -I$(top_srcdir)/src/common

job_step_test_LDADD = $(top_builddir)/src/common/libcommon.la \
		$(top_builddir)/src/common/libhostlist.la \
		$(top_builddir)/src/api/libslurm.la

step_create_LDADD = $(top_builddir)/src/common/libcommon.la \
		$(top_builddir)/src/common/libhostlist.la \
		$(top_builddir)/src/api/libslurm.la

job_add_LDADD = $(top_builddir)/src/common/libcommon.la \
		$(top_builddir)/src/common/libhostlist.la \
		$(top_builddir)/src/api/libslurm.la

job_cancel_LDADD = $(top_builddir)/src/common/libcommon.la \
		$(top_builddir)/src/common/libhostlist.la \
		$(top_builddir)/src/api/libslurm.la

LDADD   = $(top_builddir)/src/common/libcommon.la \
		$(top_builddir)/src/common/libhostlist.la	\
		$(top_builddir)/src/slurmctld/agent.o \
		$(top_builddir)/src/slurmctld/job_scheduler.o \
		$(top_builddir)/src/slurmctld/node_scheduler.o \
		$(top_builddir)/src/slurmctld/partition_mgr.o \
		$(top_builddir)/src/slurmctld/step_mgr.o \
		$(top_builddir)/src/slurmctld/job_mgr.o \
		$(top_builddir)/src/slurmctld/node_mgr.o \
		$(top_builddir)/src/slurmctld/locks.o \
		$(top_builddir)/src/common/parse_spec.o  \
		$(top_builddir)/src/slurmctld/read_config.o \
		$(top_builddir)/src/api/libslurm.la \
		$(SSL_LIBS)

force :
$(LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
