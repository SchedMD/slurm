# Makefile for task plugins

SUBDIRS = cray_aries none

if HAVE_SCHED_SETAFFINITY
SUBDIRS += affinity
endif

if WITH_CGROUP
SUBDIRS += cgroup
endif
