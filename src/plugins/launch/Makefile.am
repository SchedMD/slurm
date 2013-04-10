# Makefile for launch plugins

# launch/slurm can be useful for testing on any system type
SUBDIRS = slurm

if REAL_BGQ_LOADED
SUBDIRS += runjob
endif

if HAVE_CRAY
SUBDIRS += aprun
endif

if HAVE_NRT
SUBDIRS += poe
endif
