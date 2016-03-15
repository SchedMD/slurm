#
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmd

AM_CPPFLAGS = -DLIBSLURM_SO=\"$(libdir)/libslurm.so\" \
	-I$(top_srcdir) $(HWLOC_CPPFLAGS) $(ZLIB_CPPFLAGS) $(LZ4_CPPFLAGS)

slurmd_LDADD = 					   \
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/bcast/libfile_bcast.la \
	../common/libslurmd_common.o $(HWLOC_LIBS) \
	$(top_builddir)/src/api/libslurm.o $(DL_LIBS)  \
	../common/libslurmd_reverse_tree_math.la \
	 $(ZLIB_LIBS) $(LZ4_LIBS)

slurmd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) $(HWLOC_LDFLAGS) \
	$(ZLIB_LDFLAGS) $(LZ4_LDFLAGS)
if HAVE_AIX
# We need to set maxdata back to 0 because this effects the "max memory size"
# ulimit, and the ulimit is inherited by child processes.
slurmd_LDFLAGS += -Wl,-bmaxdata:0x0
endif

SLURMD_SOURCES = \
	slurmd.c slurmd.h \
	req.c req.h \
	get_mach_stat.c get_mach_stat.h	\
	read_proc.c 	        	\
	slurmd_plugstack.c slurmd_plugstack.h

slurmd_SOURCES = $(SLURMD_SOURCES)

force:
$(slurmd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
