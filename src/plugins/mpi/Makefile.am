# Makefile for mpi plugins

if REAL_BGQ_LOADED
#on a real BGQ do not compile any mpi plugins
SUBDIRS = none
else
SUBDIRS = none openmpi pmi2
if HAVE_PMIX
SUBDIRS += pmix
endif
endif
