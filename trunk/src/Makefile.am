# make sure popt gets distributed:
EXTRA_DIST = popt

# add subdir "squeue" as soon as something is there:
if USE_INCLUDED_POPT
SUBDIRS = popt common api slurmctld slurmd srun scontrol scancel squeue sinfo test
else
SUBDIRS = common api slurmctld slurmd srun scontrol scancel squeue sinfo test
endif
