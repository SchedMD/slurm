# $Id$

if USE_INCLUDED_POPT
SUBDIRS = popt common api slurmctld slurmd srun scontrol scancel slurm-tv-clean squeue sinfo plugins
else
SUBDIRS = common api slurmctld slurmd srun scontrol scancel slurm-tv-clean squeue sinfo plugins
endif

# make sure popt gets distributed:
EXTRA_SUBDIRS = popt
