#!/bin/bash

if [ $# -ne 5 ]; then
	echo "test8.21.bash <srun_path> <squeue_path> <job_id> <job_size> <mode:1|2?"
	exit 1
fi
srun=$1
squeue=$2
job_id=$3
job_size=$4
test_mode=$5

delay_time=1
while [ $delay_time -le 60 ]
do
	$srun -N1  --test-only --immediate /bin/true
	rc=$?
	if [ $rc -eq 0 ]
	then
		break
	fi
	sleep $delay_time
	delay_time=`expr $delay_time + 1`
done

if [ $test_mode -gt 1 ]
then
	job_size=`expr $job_size + $job_size`
	sleep_time=0
else
	sleep_time=1
fi

while [ $job_size -ge 2 ]
do
	job_size=`expr $job_size / 2`
	$srun -N$job_size --test-only sleep 50 &
	sleep $sleep_time
done

$srun -N1  --test-only sleep 50 &
sleep 5
$squeue --jobs=$job_id --steps --noheader --format='Step_ID=%i MidplaneList=%N'
