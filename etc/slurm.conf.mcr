# 
# Sample /etc/slurm.conf.mcr for mcr.llnl.gov
# Author: John Doe
# Date: 11/06/2001
#
ControlMachine=mcri   ControlAddr=emcri 
#
AuthType=auth/none
#Epilog=/admin/sbin/slurm.epilog
FastSchedule=1
FirstJobId=65536
HashBase=10
HeartbeatInterval=30
InactiveLimit=120
JobCredentialPrivateKey=private.key
JobCredentialPublicCertificate=public.cert
KillWait=30
PluginDir=/var/tmp/slurm/lib/slurm
#Prioritize=/usr/local/maui/priority
#Prolog=/admin/sbin/slurm.prolog
ReturnToService=0
SlurmUser=jette
SlurmctldDebug=3
#SlurmctldLogFile=/var/tmp/slurmctld.log
SlurmctldPidFile=/var/run/slurmctld.pid
SlurmctldPort=7002
SlurmctldTimeout=300
SlurmdDebug=3
#SlurmdLogFile=/var/tmp/slurmd.log
SlurmdPidFile=/var/run/slurmd.pid
SlurmdPort=7003
SlurmdSpoolDir=/var/tmp/slurmd.spool
SlurmdTimeout=300
StateSaveLocation=/tmp/slurm.state
TmpFS=/tmp
#
# Node Configurations
#
NodeName=DEFAULT Procs=2 RealMemory=2000 TmpDisk=64000 State=UNKNOWN
NodeName=mcr[192-1151]  NodeAddr=emcr[192-1151]
#
# Partition Configurations
#
PartitionName=DEFAULT MaxTime=30 MaxNodes=1200
PartitionName=debug Nodes=mcr[192-1151] State=UP    Default=YES
