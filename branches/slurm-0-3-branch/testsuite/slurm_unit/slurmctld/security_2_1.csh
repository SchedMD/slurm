#!/bin/csh
setenv CONFIG /etc/slurm/slurm.conf
setenv DEPLOY /usr

echo "Insure that executable files are not user writable"
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

echo "Plugin directory and its contents must be non-writable"
grep PluginDir $CONFIG
ls -ld /usr/lib/slurm
ls -l  /usr/lib/slurm

grep Prolog $CONFIG
#ls -ld /admin/sbin/slurm.prolog

echo "Spool and log files must be non-writeable"
grep SlurmdSpoolDir $CONFIG
ls -ld /var/spool/slurm
grep StateSaveLocation $CONFIG
ls -ld /usr/local/tmp/slurm/adev
grep SlurmctldLogFile $CONFIG
ls -ld /var/log/slurm/slurmctld.log
