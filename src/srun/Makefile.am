AUTOMAKE_OPTIONS = foreign

INCLUDES = $(POPT_INCLUDES) 

noinst_HEADERS = opt.h

bin_PROGRAMS = srun

srun_SOURCES = srun.c opt.c env.c
srun_LDFLAGS = 
srun_LDADD = $(POPT_LIBS) 
