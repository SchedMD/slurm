#!/bin/csh
setenv CONFIG /etc/slurm/slurm.conf
setenv DEPLOY /usr

echo "Insure that files are not user writable"
ls -ld $DEPLOY/bin/srun
ls -ld $DEPLOY/bin/sinfo
ls -ld $DEPLOY/bin/squeue
ls -ld $DEPLOY/bin/scontrol
ls -ld $DEPLOY/bin/scancel
ls -ld $DEPLOY/sbin/slurmctld
ls -ld $DEPLOY/sbin/slurmd
ls -ld $CONFIG

echo "Insure that configured files are not user writable"
grep Epilog $CONFIG
#ls -ld /admin/sbin/slurm.epilog

echo "Private Key must be non-readable too"
grep JobCredential $CONFIG
ls -ld /etc/slurm/slurm.key
ls -ld /etc/slurm/slurm.cert

grep PluginDir $CONFIG
ls -ld /usr/lib/slurm
ls -l  /usr/lib/slurm

grep Prioritize $CONFIG
#echo "Prioritize will move to a plugin"

grep Prolog $CONFIG
#ls -ld /admin/sbin/slurm.prolog

grep SlurmdSpoolDir $CONFIG
ls -ld /tmp/slurmd

grep StateSaveLocation $CONFIG
ls -ld /usr/local/tmp/slurm/adev
