# Makefile for sreport

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = sreport

sreport_SOURCES =	\
	sreport.c sreport.h \
	cluster_reports.c cluster_reports.h \
	job_reports.c job_reports.h	\
	user_reports.c user_reports.h	\
	resv_reports.c resv_reports.h	\
	common.c

sreport_LDADD =  $(LIB_SLURM) $(DL_LIBS) $(READLINE_LIBS)
sreport_DEPENDENCIES = $(LIB_SLURM_BUILD)

sreport_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(sreport_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
