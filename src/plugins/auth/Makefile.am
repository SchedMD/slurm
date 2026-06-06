# Makefile for auth plugins

SUBDIRS = common none

if WITH_MUNGE
SUBDIRS += munge
endif

if WITH_JWT
SUBDIRS += jwt slurm
endif
