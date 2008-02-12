# Makefile for sacctmgr

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = scontrol

scontrol_SOURCES =	\
	user_functions.c	\
	cluster_functions.c	\
	account_functions.c	\
	association_functions.c   \
	sacctmgr.c	\
	sacctmgr.h	

convenience_libs = $(top_builddir)/src/api/libslurmhelper.la

scontrol_LDADD = \
	$(convenience_libs) \
	$(READLINE_LIBS)

scontrol_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
