#!/bin/csh
setenv CONFIG /etc/slurm.conf
setenv DEPLOY /usr/local

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
ls -ld /admin/sbin/slurm.epilog

grep Prolog $CONFIG
ls -ld /admin/sbin/slurm.prolog

echo "Private Key must be non-readable too"
grep JobCredential $CONFIG
ls -ld /usr/local/slurm/private.key
ls -ld /usr/local/slurm/public.cert

grep PluginDir $CONFIG
ls -ld /usr/local/lib/slurm
ls -l  /usr/local/lib/slurm

grep SlurmdSpoolDir $CONFIG
ls -ld /tmp/slurmd.spool

grep StateSaveLocation $CONFIG
ls -ld /tmp/slurm.state
