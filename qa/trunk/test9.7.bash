#!/bin/bash
# Simple SLURM stress test
# Usage: <prog> <sleep_time> <iterations>
# Default is 1 second sleep and 3 iterations
if [ $# -gt 0 ]; then
	sleep_time=$1
else
	sleep_time=1
fi

if [ $# -gt 1 ]; then
	iterations=$2
else
	iterations=3
fi

exit_code=0
inx=1
log="test9.7.$$.output"
touch $log
while [ $inx -le $iterations ]
do
	echo "########## LOOP $inx ########## " >>$log 2>&1
	sinfo                                   >>$log 2>&1
	if [ $? -ne 0 ]; then
		exit_code=$?
	fi
	sleep $sleep_time
	srun -N1-$inx -c1 -l hostname           >>$log 2>&1
	if [ $? -ne 0 ]; then
		exit_code=$?
	fi
	sleep $sleep_time
	squeue                                  >>$log 2>&1
	if [ $? -ne 0 ]; then
		exit_code=$?
	fi
	sleep $sleep_time
	inx=$((inx+1))
done

if [ $exit_code -ne 0 ]; then
	cat $log
fi
rm $log
echo "########## EXIT_CODE $exit_code ########## "
exit $exit_code
