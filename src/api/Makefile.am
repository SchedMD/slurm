# $Id$
#
# Makefile for slurmlib

AUTOMAKE_OPTIONS = foreign

EXTRA_DIST = slurm.h

INCLUDES = -I$(top_srcdir)/src/common

# should we build individual modules?
# (--enable-debug)
#
if DEBUG_MODULES
test_modules = allocate build_info cancel job_info node_info partition_info reconfigure submit update_config
else
test_modules = 
endif

# link to libslurm (for test modules):
LDADD = -lslurm

lib_LTLIBRARIES = libslurm.la

libslurm_la_SOURCES =    \
	allocate.c       \
	build_info.c     \
	job_info.c       \
	node_info.c      \
	partition_info.c \
	submit.c         \
	reconfigure.c    \
	update_config.c  

common_dir = $(top_srcdir)/src/common

#libslurm_la_LIBADD        = \
#	$(common_dir)/pack.lo 	 	\
#	$(common_dir)/xmalloc.lo 	\
#	$(common_dir)/log.lo		\
#	$(common_dir)/safeopen.lo	\
#	$(common_dir)/xassert.c		\
#	$(common_dir)/xstring.lo	

libslurm_la_LIBADD        = $(common_dir)/libcommon.la
libslurm_la_LDFLAGS       = -export-symbols libslurm.sym
libslurm_la_DEPENDENCIES  = libslurm.sym $(libslurm_la_LIBADD)

libslurm.sym :
	cp slurmapi.exp libslurm.sym

# hack to allow building .lo objects from other directories
../*/*.lo : %.lo : %.c
	cd `echo $@|sed 's#/[^/]*\$$##'` && $(MAKE) `echo $@|sed 's#.*/##'`
#$(filter %.lo, $(libslurm_la_LIBADD)) : %.lo : %.c
#	cd `echo $@|sed 's#/[^/]*\$$##'` && $(MAKE) `echo $@|sed 's#.*/##'`
	
#$(filter %.la, $(libslurm_la_LIBADD)) : %.la : 
#	cd `echo $@|sed 's#/[^/]*\$$##'` && $(MAKE) `echo $@|sed 's#.*/##'`

# target specific CFLAGS do not work until automake 1.5, use explicit rules
# for now. If we ever move to 1.5, the CFLAGS defs below should work in
# place of the rules that follow.

#allocate_CFLAGS        = -DDEBUG_MODULE $(AM_CFLAGS)
#build_info_CFLAGS      = -DDEBUG_MODULE $(AM_CFLAGS)
#cancel_CFLAGS          = -DDEBUG_MODULE $(AM_CFLAGS)
#job_info_CFLAGS        = -DDEBUG_MODULE $(AM_CFLAGS)
#node_info_CFLAGS       = -DDEBUG_MODULE $(AM_CFLAGS)
#partition_info_CFLAGS  = -DDEBUG_MODULE $(AM_CFLAGS)
#reconfigure_CFLAGS     = -DDEBUG_MODULE $(AM_CFLAGS)
#submit_CFLAGS          = -DDEBUG_MODULE $(AM_CFLAGS)
#update_confige_CFLAGS  = -DDEBUG_MODULE $(AM_CFLAGS)

# debug modules to built as binaries:
#
# need to create a rule to build each binary from an object file with _d suffix
# as these will have been compiled with -DDEBUG_MODULE
#
# Note: automake 1.5 will complain about these...
#
allocate       : allocate_d.o
build_info     : build_info_d.o
cancel         : cancel_d.o
job_info       : job_info_d.o
node_info      : node_info_d.o
partition_info : partition_info_d.o
reconfigure    : reconfigure_d.o
submit         : submit_d.o
update_config  : update_config_d.o

% : %_d.o
	$(LINK) $(LDFLAGS) $^ $(LDADD) $(LIBS)

%_d.o : %.c
	$(COMPILE) -DDEBUG_MODULE -c -o $@ $< 
