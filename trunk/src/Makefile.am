# make sure popt gets distributed:
EXTRA_DIST = popt

# add subdirs "scancel" and "squeue" as soon as something is there:
if USE_INCLUDED_POPT
SUBDIRS = popt common api slurmctld slurmd srun scontrol test
else
SUBDIRS = common api slurmctld slurmd srun scontrol test
endif
