#!/bin/bash
# Create a dummy slurm.conf file with different munge socket
cwd=`pwd`
rm -f $cwd/slurm.conf
grep -v AccountingStoragePass </etc/slurm/slurm.conf >$cwd/slurm.conf
echo "AccountingStoragePass=$cwd/private.socket" >>$cwd/slurm.conf

# Rum sacctmgr with this different slurm.conf
export SLURM_CONF=$cwd/slurm.conf
sacctmgr show cluster
