AUTOMAKE_OPTIONS = foreign

INCLUDES = $(POPT_INCLUDES) 

bin_PROGRAMS = srun

srun_SOURCES = srun.c opt.c env.c opt.h env.h job.c job.h net.c net.h \
	       msg.c msg.h io.c io.h launch.h launch.c
srun_LDFLAGS = \
	$(top_srcdir)/src/common/libcommon.la \
	$(top_srcdir)/src/api/libslurm.la
srun_LDADD = $(POPT_LIBS) 
