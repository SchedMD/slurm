# $Id$

if USE_INCLUDED_POPT
SUBDIRS = popt common api slurmctld slurmd srun scontrol scancel squeue sinfo
else
SUBDIRS = common api slurmctld slurmd srun scontrol scancel squeue sinfo
endif

# make sure popt gets distributed:
EXTRA_SUBDIRS = popt
