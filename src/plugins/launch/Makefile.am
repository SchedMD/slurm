# Makefile for launch plugins

if REAL_BGQ_LOADED
SUBDIRS = runjob
else
SUBDIRS = slurm
if HAVE_NRT
SUBDIRS += poe
endif
endif
