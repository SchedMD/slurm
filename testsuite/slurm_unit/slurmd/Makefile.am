AUTOMAKE_OPTIONS = foreign

noinst_PROGRAMS = task_mgr-test

INCLUDES = -I$(top_srcdir)/src/common
LDADD   = $(top_srcdir)/src/common/libcommon.la \
	$(top_srcdir)/src/slurmd/task_mgr.o \
	$(top_srcdir)/src/slurmd/shmem_struct.o
