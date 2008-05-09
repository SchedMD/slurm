# Makefile for sacctmgr

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sacctmgr

sacctmgr_SOURCES =	\
	account_functions.c	\
	cluster_functions.c	\
	common.c                \
	print.c	print.h	        \
	sacctmgr.c sacctmgr.h	\
	user_functions.c	

convenience_libs = $(top_builddir)/src/api/libslurmhelper.la

sacctmgr_LDADD = \
	$(convenience_libs) \
	$(READLINE_LIBS)

sacctmgr_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(top_builddir)/src/common/libcommon.o -ldl

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
