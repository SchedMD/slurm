AUTOMAKE_OPTIONS = foreign

noinst_PROGRAMS = cancel-tst \
		complete-tst \
		allocate-tst \
		job_info-tst \
		node_info-tst \
		partition_info-tst \
		reconfigure-tst \
		submit-tst \
		update_config-tst
		

INCLUDES = -I$(top_srcdir)/src/common
LDADD   = $(top_srcdir)/src/common/libcommon.la \
		$(top_srcdir)/src/api/libslurm.la
