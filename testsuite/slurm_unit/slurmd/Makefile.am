AUTOMAKE_OPTIONS = foreign

noinst_PROGRAMS = task_mgr-test circular_buffer-test task_launch-test

INCLUDES = -I$(top_srcdir)/src/common
LDADD   = $(top_srcdir)/src/common/libcommon.la \
	$(top_srcdir)/src/slurmd/task_mgr.o \
	$(top_srcdir)/src/slurmd/shmem_struct.o \
	$(top_srcdir)/src/slurmd/circular_buffer.o
