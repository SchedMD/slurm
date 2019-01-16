# Makefile for sacctmgr

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sacctmgr

sacctmgr_DEPENDENCIES = $(LIB_SLURM_BUILD)

sacctmgr_LDADD = $(LIB_SLURM) $(DL_LIBS) $(READLINE_LIBS)

sacctmgr_SOURCES =	\
	account_functions.c	\
	archive_functions.c	\
	association_functions.c	\
	config_functions.c	\
	cluster_functions.c	\
	common.c                \
	event_functions.c	\
	federation_functions.c	\
	file_functions.c	\
	runaway_job_functions.c \
	job_functions.c		\
	reservation_functions.c	\
	resource_functions.c	\
	sacctmgr.c sacctmgr.h	\
	qos_functions.c		\
	txn_functions.c		\
	user_functions.c	\
	wckey_functions.c	\
	problem_functions.c     \
	tres_function.c

sacctmgr_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(sacctmgr_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
