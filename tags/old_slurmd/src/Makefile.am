# make sure popt gets distributed:
EXTRA_DIST = popt

if USE_INCLUDED_POPT
SUBDIRS = popt common api slurmctld slurmd srun scontrol scancel squeue sinfo
else
SUBDIRS = common api slurmctld slurmd srun scontrol scancel squeue sinfo
endif
