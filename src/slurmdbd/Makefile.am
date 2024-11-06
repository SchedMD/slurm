#
# Makefile for slurmdbd

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

sbin_PROGRAMS = slurmdbd

slurmdbd_LDADD = $(LIB_SLURM)

slurmdbd_SOURCES = 		\
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

slurmdbd_LDFLAGS = $(CMD_LDFLAGS)

slurmdbd_DEPENDENCIES = $(LIB_SLURM_BUILD)

force:
$(slurmdbd_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
