AUTOMAKE_OPTIONS = foreign

INCLUDES = pack.h bitstring.h qsw.h

bin_PROGRAMS = runqsw

srun_SOURCES = runqsw.c qsw.c bitstring.c pack.c
srun_LDFLAGS = 
srun_LDADD = -lrms -lelan3 -lpthread
