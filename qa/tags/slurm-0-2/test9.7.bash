#!/bin/bash
# Simple SLURM stress test
# Usage: <prog> <exec1> <exec2> <exec3> <sleep_time> <iterations>
# Default is sinfo, srun, squeue, 1 second sleep and 3 iterations
if [ $# -gt 0 ]; then
	exec1=$1
else
	exec1="sinfo"
fi
if [ $# -gt 1 ]; then
	exec2=$2
else
	exec2="srun"
fi
if [ $# -gt 2 ]; then
	exec3=$3
else
	exec3="squeue"
fi
if [ $# -gt 3 ]; then
	sleep_time=$4
else
	sleep_time=1
fi

if [ $# -gt 4 ]; then
	iterations=$5
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
	$exec1                                  >>$log 2>&1
	if [ $? -ne 0 ]; then
		exit_code=$?
	fi
	sleep $sleep_time
	$exec2 -N1-$inx -c1 -l hostname         >>$log 2>&1
	if [ $? -ne 0 ]; then
		exit_code=$?
	fi
	sleep $sleep_time
	$exec3                                  >>$log 2>&1
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
