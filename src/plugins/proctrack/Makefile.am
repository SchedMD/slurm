# Makefile for proctrack plugins

if HAVE_NATIVE_CRAY
CRAY = cray_aries
endif

SUBDIRS = $(CRAY) linuxproc pgid

if WITH_CGROUP
SUBDIRS += cgroup
endif
