#!/bin/bash
# Simple SLURM stress test
# Usage: <prog> <sleep_time> <iterations>
# Default is 1 second sleep and 3 iterations
if [ $# -gt 1 ]; then
	sleep_time=$1
else
	sleep_time=1
fi

if [ $# -gt 3 ]; then
	iterations=$2
else
	iterations=3
fi

exit_code=0
inx=1
while [ $inx -le $iterations ]
do
	echo "########## LOOP $inx ########## "
	sinfo
	if [ $? -ne 0 ]; then
		exit_code=$?
	fi
	sleep $sleep_time
	srun -N1-$inx -c1 -l hostname
	if [ $? -ne 0 ]; then
		exit_code=$?
	fi
	sleep $sleep_time
	squeue
	if [ $? -ne 0 ]; then
		exit_code=$?
	fi
	sleep $sleep_time
	inx=$((inx+1))
done

echo "########## EXIT_CODE $exit_code ########## "
exit $exit_code
