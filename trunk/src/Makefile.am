
popt_dirs =

if USE_INCLUDED_POPT
  popt_dirs = popt
else 
  popt_dirs =
endif

# make sure popt gets distributed:
EXTRA_DIST = popt

# add subdirs "scancel" and "squeue" as soon as something is there:
SUBDIRS = $(popt_dirs) common slurmctld slurmd srun scontrol 
