#!/bin/sh
# for a file containing a list of allocated nodes, get the slurm
# jobid for the job running on that node for this user
#
node=`head -n 1 $1`
user=`id -un`
jobid=`squeue -h -o %i -u $user -n $node`
echo $jobid
