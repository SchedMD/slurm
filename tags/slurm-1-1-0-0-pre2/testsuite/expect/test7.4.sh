#!/bin/sh
# Usage: <squeue path> <node list>
# Returns SLURM job id allocated  to that node for that user
#
node=`head -n 1 $2`
user=`id -un`
jobid=`$1 -h -o %i -u $user -n $node`
echo $jobid
