AUTOMAKE_OPTIONS = foreign

#noinst_PROGRAMS = task_mgr-test circular_buffer-test task_launch-test credential_util-test

if WITH_ELAN
interconnect_lib = $(top_srcdir)/src/slurmd/libelan_interconnect.la
process_lib = $(top_srcdir)/src/slurmd/libforked.la
io_lib = $(top_srcdir)/src/slurmd/libnbio.la
else
interconnect_lib = $(top_srcdir)/src/slurmd/libno_interconnect.la
process_lib = $(top_srcdir)/src/slurmd/libthreaded.la
io_lib = $(top_srcdir)/src/slurmd/libthreaded_io.la
endif
	

INCLUDES = -I$(top_srcdir)/src/common
LDADD   = $(top_srcdir)/src/common/libcommon.la \
	$(top_srcdir)/src/slurmd/task_mgr.o \
	$(top_srcdir)/src/slurmd/shmem_struct.o \
	$(top_srcdir)/src/slurmd/circular_buffer.o \
	$(top_srcdir)/src/slurmd/threaded_io.o \
	$(top_srcdir)/src/slurmd/reconnect_utils.o \
	$(top_srcdir)/src/slurmd/io_threads.o \
	$(top_srcdir)/src/slurmd/pipes.o \
	$(top_srcdir)/src/slurmd/setenvpf.o \
	$(top_srcdir)/src/common/libcred.la \
	$(SSL_LIBS) \
	$(interconnect_lib) \
	$(process_lib) \
	$(io_lib)

