#
# Makefile for slurmdbd

AUTOMAKE_OPTIONS = foreign

SLURMDBD_SOURCES = 		\
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
	slurmdbd.c

if ! USE_LOADLEVELER

CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

sbin_PROGRAMS = slurmdbd

slurmdbd_LDADD = 					\
	$(top_builddir)/src/common/libdaemonize.la \
	$(top_builddir)/src/api/libslurm.o -ldl


slurmdbd_SOURCES = $(SLURMDBD_SOURCES)

slurmdbd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(slurmdbd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_slurmdbd_SOURCES = $(SLURMDBD_SOURCES)
endif
