# Makefile for launch plugins

if REAL_BGQ_LOADED
SUBDIRS = runjob
else
if HAVE_CRAY
SUBDIRS = aprun
else
SUBDIRS = slurm
if HAVE_NRT
SUBDIRS += poe
endif
endif
endif
