# Makefile for proctrack plugins

if HAVE_AIX_PROCTRACK
AIX_PROCTRACK = aix
endif

# On a native cray the sgi_job is similar but different enough we felt we
# wanted to make a new plugin to not break other implementations.

if HAVE_NATIVE_CRAY
CRAY = cray
else
if HAVE_SGI_JOB
SGI_JOB = sgi_job
endif
endif

if HAVE_LUA
LUA = lua
endif

SUBDIRS = $(AIX_PROCTRACK) $(CRAY) $(SGI_JOB) $(LUA) pgid linuxproc cgroup
