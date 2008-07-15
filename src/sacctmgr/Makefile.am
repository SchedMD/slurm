# Makefile for sacctmgr

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sacctmgr

sacctmgr_LDADD =  \
	$(top_builddir)/src/api/libslurm.o -ldl\
	$(READLINE_LIBS)

sacctmgr_SOURCES =	\
	account_functions.c	\
	association_functions.c	\
	cluster_functions.c	\
	common.c                \
	file_functions.c	\
	sacctmgr.c sacctmgr.h	\
	qos_functions.c		\
	txn_functions.c		\
	user_functions.c	

sacctmgr_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(sacctmgr_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
