# Makefile for sacctmgr

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sacctmgr

sacctmgr_SOURCES =	\
	account_functions.c	\
	association_functions.c	\
	cluster_functions.c	\
	common.c                \
	file_functions.c	\
	sacctmgr.c sacctmgr.h	\
	user_functions.c	

sacctmgr_LDADD =  \
	$(top_builddir)/src/common/libcommon.o -ldl \
	$(top_builddir)/src/api/libslurmhelper.la \
	$(READLINE_LIBS)

sacctmgr_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
