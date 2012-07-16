# Makefile for sreport

AUTOMAKE_OPTIONS = foreign
CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sreport

sreport_SOURCES =	\
	sreport.c sreport.h \
	cluster_reports.c cluster_reports.h \
	assoc_reports.c assoc_reports.h	\
	job_reports.c job_reports.h	\
	user_reports.c user_reports.h	\
	resv_reports.c resv_reports.h	\
	common.c

sreport_LDADD =  \
	$(top_builddir)/src/db_api/libslurmdb.o $(DL_LIBS) \
	$(READLINE_LIBS)

sreport_LDFLAGS = -export-dynamic $(CMD_LDFLAGS) \
	$(HWLOC_LDFLAGS) $(HWLOC_LIBS)

force:
$(sreport_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
