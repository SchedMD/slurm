# Makefile for launch plugins

if REAL_BGQ_LOADED
SUBDIRS = runjob
else
# launch/slurm can be useful for testing on any system other type
SUBDIRS = slurm
if HAVE_NRT
SUBDIRS += poe
endif
if HAVE_REAL_CRAY
SUBDIRS += aprun
endif
if HAVE_ALPS_EMULATION
SUBDIRS += aprun
endif
endif
