# $Id$
#
# Makefile for slurmctld

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)/src/common
CFLAGS = 

# should we build individual modules?
# (--enable-debug)
#
if DEBUG_MODULES
test_modules = controller job_mgr job_scheduler node_mgr node_scheduler partition_mgr read_config step_mgr
else
test_modules = 
endif

bin_PROGRAMS = slurmctld

noinst_PROGRAMS = $(test_modules)

#LDADD = $(top_srcdir)/src/api/libslurmapi.a $(top_srcdir)/src/common/libcommon.a
LDADD   = $(top_srcdir)/src/api/nodelist.lo $(top_srcdir)/src/common/libcommon.la 

slurmctld_SOURCES = 	\
	util.c		\
	parse_spec.c	\
	controller.c 	\
	job_mgr.c 	\
	job_scheduler.c \
	node_mgr.c 	\
	node_scheduler.c\
	partition_mgr.c \
	read_config.c	\
	step_mgr.c

# target specific CFLAGS do not work until automake 1.5, use explicit rules
# for now. If we ever move to 1.5, the CFLAGS defs below should work in
# place of the rules that follow.

#controller_CFLAGS     = -DDEBUG_MODULE $(AM_CFLAGS)
#job_mgr_CFLAGS        = -DDEBUG_MODULE $(AM_CFLAGS)
#job_scheduler_CFLAGS  = -DDEBUG_MODULE $(AM_CFLAGS)
#node_mgr_CFLAGS       = -DDEBUG_MODULE $(AM_CFLAGS)
#node_scheduler_CFLAGS = -DDEBUG_MODULE $(AM_CFLAGS)
#partition_mgr_CFLAGS  = -DDEBUG_MODULE $(AM_CFLAGS)
#read_config_CFLAGS    = -DDEBUG_MODULE $(AM_CFLAGS)
#step_mgr_CFLAGS       = -DDEBUG_MODULE $(AM_CFLAGS)

# debug modules to built as binaries:
#
# need to create a rule to build each binary from an object file with _d suffix
# as these will have been compiled with -DDEBUG_MODULE
#
# Note: automake 1.5 will complain about these...
#
controller$(EXEEXT)     : controller_d.o job_mgr.o job_scheduler.o node_mgr.o node_scheduler.o partition_mgr.o read_config.o step_mgr.o
job_mgr$(EXEEXT)        : job_mgr_d.o node_mgr.o node_scheduler.o partition_mgr.o
job_scheduler$(EXEEXT)  : job_scheduler_d.o job_mgr.o node_mgr.o node_scheduler.o partition_mgr.o
node_mgr$(EXEEXT)       : node_mgr_d.o  
node_scheduler$(EXEEXT) : node_scheduler_d.o job_mgr.o node_mgr.o partition_mgr.o read_config.o
partition_mgr$(EXEEXT)  : partition_mgr_d.o node_mgr.o
read_config$(EXEEXT)    : read_config_d.o job_mgr.o node_mgr.o node_scheduler.o partition_mgr.o step_mgr.o
step_mgr$(EXEEXT)       : step_mgr_d.o job_mgr.o node_mgr.o node_scheduler.o partition_mgr.o

% : %_d.o
	$(LINK) $(LDFLAGS) $^ $(LDADD) $(LIBS)

%_d.o : %.c
	$(COMPILE) -DDEBUG_MODULE -c -o $@ $< 
