# Makefile for mpi plugins

if REAL_BGQ_LOADED
#on a real BGQ do not compile any mpi plugins
SUBDIRS = none
else
SUBDIRS = mpich1_p4 mpich1_shmem mpichgm mpichmx mvapich none lam openmpi pmi2
endif
