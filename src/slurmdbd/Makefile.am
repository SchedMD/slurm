#
# Makefile for slurmdbd

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

sbin_PROGRAMS = slurmdbd

slurmdbd_LDADD = 					\
	$(top_builddir)/src/common/libdaemonize.la 


slurmdbd_SOURCES = 		\
	agent.c			\
	agent.h			\
	proc_req.c		\
	proc_req.h		\
	read_config.c		\
	read_config.h		\
	rpc_mgr.c		\
	rpc_mgr.h		\
	slurmdbd.c  		\
	slurmdbd.h

slurmdbd_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	-Wl,--whole-archive,$(top_builddir)/src/common/.libs/libcommon.a,\
	-Wl,-no-whole-archive -ldl

force:
$(slurmdbd_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
