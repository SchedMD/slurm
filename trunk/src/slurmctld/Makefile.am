# $Id$
#
# Makefile for slurmctld

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = $(SSL_CPPFLAGS)

bin_PROGRAMS = slurmctld

LDADD = $(top_srcdir)/src/common/libcommon.la    \
	$(top_srcdir)/src/common/libcred.la      \
	$(top_srcdir)/src/common/libdaemonize.la 


slurmctld_SOURCES = 	\
	agent.h		\
	locks.h  	\
	slurmctld.h	\
	agent.c		\
	controller.c 	\
	job_mgr.c 	\
	job_scheduler.c \
	locks.c   	\
	node_mgr.c 	\
	node_scheduler.c\
	partition_mgr.c \
	read_config.c	\
	step_mgr.c	\
	pack.c 

% : %_d.o
	$(LINK) $(LDFLAGS) $^ $(LDADD) $(LIBS)

%_d.o : %.c
	$(COMPILE) -DDEBUG_MODULE -c -o $@ $< 
