#
# Makefile for slurmdbd

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

sbin_PROGRAMS = slurmdbd

slurmdbd_LDADD = 					\
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/api/libslurm.o $(DL_LIBS)


slurmdbd_SOURCES = 		\
	agent.c			\
	agent.h			\
	backup.c		\
	backup.h		\
	proc_req.c		\
	proc_req.h		\
	read_config.c		\
	read_config.h		\
	rpc_mgr.c		\
	rpc_mgr.h		\
	slurmdbd.c  		\
	slurmdbd.h

slurmdbd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(slurmdbd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
