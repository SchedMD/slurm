# Makefile for proctrack plugins

if HAVE_AIX_PROCTRACK
AIX_PROCTRACK = aix 
endif

if HAVE_SGI_JOB
SGI_JOB = sgi_job
endif

if HAVE_LUA
LUA = lua
endif

SUBDIRS = $(AIX_PROCTRACK) $(SGI_JOB) $(LUA) pgid linuxproc rms
