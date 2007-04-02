#!/bin/sh
# Usage: <squeue path> <hostlist file>
# Returns SLURM job id allocated  to that node for that user
# Note: The hostlist file contains full pathnames that need to 
#	be stripped to the hostname (e.g. "linux123.llnl.gov" 
#	becomes "linux123" as an argument so squeue).
#
node=`head -n 1 $2 | cut -f 1 -d .`
user=`id -un`
jobid=`$1 -h -o %i -u $user -n $node`
echo $jobid
