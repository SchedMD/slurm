# Makefile for scontrol

AUTOMAKE_OPTIONS = foreign

AM_CPPFLAGS = -I$(top_srcdir)

bin_PROGRAMS = scontrol

scontrol_SOURCES =	\
	create_res.c	\
	info_burst_buffer.c \
	info_assoc_mgr.c \
	info_fed.c 	\
	info_job.c	\
	info_layout.c	\
	info_lics.c	\
	info_node.c	\
	info_part.c	\
	info_res.c	\
	reboot_node.c	\
	scontrol.c	\
	scontrol.h	\
	update_job.c	\
	update_layout.c \
	update_node.c	\
	update_part.c	\
	update_step.c   \
	update_powercap.c

scontrol_LDADD = $(LIB_SLURM) $(DL_LIBS) -lm $(READLINE_LIBS)
scontrol_DEPENDENCIES = $(LIB_SLURM_BUILD)

scontrol_LDFLAGS = -export-dynamic $(CMD_LDFLAGS)

force:
$(scontrol_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
