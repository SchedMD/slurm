# Makefile for scontrol

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir) $(BG_INCLUDES)

bin_PROGRAMS = scontrol

scontrol_SOURCES =	\
	create_res.c	\
	info_block.c	\
	info_burst_buffer.c \
	info_assoc_mgr.c \
	info_fed.c 	\
	info_job.c	\
	info_layout.c	\
	info_lics.c	\
	info_node.c	\
	info_part.c	\
	info_res.c	\
	scontrol.c	\
	scontrol.h	\
	update_job.c	\
	update_layout.c \
	update_node.c	\
	update_part.c	\
	update_step.c   \
	update_powercap.c

convenience_libs = $(top_builddir)/src/api/libslurm.o $(DL_LIBS) -lm


scontrol_LDADD = \
	$(convenience_libs) \
	$(READLINE_LIBS)

scontrol_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(convenience_libs) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
