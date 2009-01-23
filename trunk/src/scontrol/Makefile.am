# Makefile for scontrol

AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir)

bin_PROGRAMS = scontrol

scontrol_SOURCES =	\
	create_res.c	\
	info_job.c	\
	info_node.c	\
	info_part.c	\
	info_res.c	\
	scontrol.c	\
	scontrol.h	\
	update_job.c	\
	update_node.c	\
	update_part.c

convenience_libs = $(top_builddir)/src/api/libslurm.o -ldl -lm


scontrol_LDADD = \
	$(convenience_libs) \
	$(READLINE_LIBS)

scontrol_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
