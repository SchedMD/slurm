AUTOMAKE_OPTIONS = foreign

INCLUDES = $(POPT_INCLUDES) 

bin_PROGRAMS = srun

srun_SOURCES = srun.c opt.c env.c opt.h env.h
srun_LDFLAGS = \
	$(top_srcdir)/src/common/libcommon.la \
	$(top_srcdir)/src/api/libslurm.la
srun_LDADD = $(POPT_LIBS) 
