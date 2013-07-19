# Makefile for launch plugins

if REAL_BGQ_LOADED
SUBDIRS = runjob
else
# launch/slurm can be useful for testing on any system other type
SUBDIRS = slurm
if HAVE_NRT
SUBDIRS += poe
endif
if HAVE_ALPS_CRAY
SUBDIRS += aprun
endif
endif
