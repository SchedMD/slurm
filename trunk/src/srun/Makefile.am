AUTOMAKE_OPTIONS = foreign

INCLUDES = -I$(top_srcdir) $(POPT_INCLUDES) 

if WITH_TOTALVIEW
totalview = slurm_auth_authd.c
else
auth_sources = slurm_auth_authd.c
endif

bin_PROGRAMS = srun

srun_SOURCES = srun.c opt.c env.c opt.h env.h job.c job.h net.c net.h \
	       msg.c msg.h io.c io.h launch.h launch.c attach.h       \
               reattach.c reattach.h fname.h fname.c
srun_LDADD = $(top_builddir)/src/common/libcommon.la \
	     $(top_builddir)/src/api/libslurm.la \
	     $(POPT_LIBS) $(TOTALVIEW_LIBS)
