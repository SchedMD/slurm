#!/bin/csh
setenv CONFIG /etc/slurm
setenv DEPLOY /usr

echo ""
echo "Insure that executable files are not world writable"
ls -ld $DEPLOY/bin/srun
ls -ld $DEPLOY/bin/sacct
ls -ld $DEPLOY/bin/sinfo
ls -ld $DEPLOY/bin/squeue
ls -ld $DEPLOY/bin/scontrol
ls -ld $DEPLOY/bin/scancel
ls -ld $DEPLOY/sbin/slurm*

echo ""
echo "Insure that configuration directory and files are not world writable"
ls -ld $CONFIG
ls -l  $CONFIG

echo ""
echo "Insure that configured files are not world writable"
grep "Prolog=" $CONFIG/slurm.conf
ls -ld /etc/slurm/prolog
ls -ld $DEPLOY/sbin/slurm_prolog
echo ""
grep "Epilog=" $CONFIG/slurm.conf
ls -ld /etc/slurm/epilog
ls -ld $DEPLOY/sbin/slurm_epilog

echo ""
echo "Both Job Keys are not world readable"
echo "Private Key must not be world readable too"
grep "JobCredentialPrivateKey=" $CONFIG/slurm.conf
ls -ld /etc/slurm/slurm.key
echo ""
grep "JobCredentialPublicCertificate=" $CONFIG/slurm.conf
ls -ld /etc/slurm/slurm.cert

echo ""
echo "Plugin directory and its contents must not be world writable"
grep "PluginDir=" $CONFIG/slurm.conf
ls -ld /usr/lib*/slurm
ls -l  /usr/lib*/slurm

echo ""
echo "Spool and log files must be not be world writeable"
grep "SlurmdSpoolDir=" $CONFIG/slurm.conf
grep "StateSaveLocation=" $CONFIG/slurm.conf
grep "SlurmctldLogFile=" $CONFIG/slurm.conf
grep "SlurmdLogFile=" $CONFIG/slurm.conf
grep "JobCompLog=" $CONFIG/slurm.conf
ls -ld /usr/local/tmp/slurm/bgl
ls -l  /usr/local/tmp/slurm/bgl
ls -ld /var/log/slurm*
ls -l  /var/log/slurm*
