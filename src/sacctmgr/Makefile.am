# Makefile for sacctmgr

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

SACCTMGR_SOURCES =		\
	account_functions.c	\
	archive_functions.c	\
	association_functions.c	\
	config_functions.c	\
	cluster_functions.c	\
	common.c                \
	event_functions.c	\
	file_functions.c	\
	job_functions.c		\
	problem_functions.c	\
	sacctmgr.c sacctmgr.h	\
	qos_functions.c		\
	txn_functions.c		\
	user_functions.c	\
	wckey_functions.c

if ! USE_LOADLEVELER

bin_PROGRAMS = sacctmgr

sacctmgr_LDADD =  $(top_builddir)/src/db_api/libslurmdb.o -ldl \
	$(READLINE_LIBS)

sacctmgr_SOURCES = $(SACCTMGR_SOURCES)

sacctmgr_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(sacctmgr_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_sacctmgr_SOURCES = $(SACCTMGR_SOURCES)
endif
