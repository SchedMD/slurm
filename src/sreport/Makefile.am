# Makefile for sreport

AUTOMAKE_OPTIONS = foreign

SREPORT_SOURCES =		\
	assoc_reports.c		\
	assoc_reports.h		\
	cluster_reports.c	\
	cluster_reports.h	\
	common.c		\
	job_reports.c		\
	job_reports.h		\
	resv_reports.c		\
	resv_reports.h		\
	sreport.c		\
	sreport.h		\
	user_reports.c		\
	user_reports.h

if ! USE_LOADLEVELER

CLEANFILES = core.*

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = sreport

sreport_SOURCES = $(SREPORT_SOURCES)

sreport_LDADD =  \
	$(top_builddir)/src/db_api/libslurmdb.o -ldl \
	$(READLINE_LIBS)

sreport_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(sreport_LDADD) : force
	@cd `dirname $@` && $(MAKE) `basename $@`

else
EXTRA_sreport_SOURCES =	$(SREPORT_SOURCES)
endif
