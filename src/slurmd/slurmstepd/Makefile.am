#
# Makefile for slurmd

AUTOMAKE_OPTIONS = foreign

sbin_PROGRAMS = slurmstepd

depend_libs = ../common/libslurmd_common.o

# If slurmstepd_LDADD or slurmstepd_LDFLAGS change
# make sure you alter the below depend_[ldadd|ldflags] to look identical in the
# slurmd.  If these libs aren't available
# on slurmstepd startup the slurmstepd will fail sliently and make debugging
# rather difficult.  Making the slurmd link to them as well shouldn't increase
# memory footprint and will fail way earlier and give the user a way to known
# what is happening.
depend_ldadd = $(HWLOC_LIBS) $(PAM_LIBS) $(UTIL_LIBS)
depend_ldflags = $(HWLOC_LDFLAGS)

AM_CPPFLAGS = -I$(top_srcdir) $(HWLOC_CPPFLAGS)

slurmstepd_LDADD = $(depend_libs) $(LIB_SLURM) $(depend_ldadd) $(libselinux_LIBS) $(SLURMD_INTERFACES)

slurmstepd_LDFLAGS = $(CMD_LDFLAGS) $(depend_ldflags)

slurmstepd_SOURCES = 	        	\
	container.c container.h	\
	slurmstepd.c slurmstepd.h	\
	mgr.c mgr.h			\
	task.c task.h			\
	slurmstepd_job.c slurmstepd_job.h \
	io.c io.h			\
	ulimits.c ulimits.h     	\
	pdebug.c pdebug.h		\
	pam_ses.c pam_ses.h		\
	req.c req.h			\
	multi_prog.c multi_prog.h	\
	step_terminate_monitor.c step_terminate_monitor.h \
	x11_forwarding.c x11_forwarding.h

slurmstepd_DEPENDENCIES = $(depend_libs) $(LIB_SLURM_BUILD) $(SLURMD_INTERFACES)

force:
$(slurmstepd_DEPENDENCIES) : force
	@cd `dirname $@` && $(MAKE) `basename $@`
