#!/bin/bash

if [ $# -ne 4 ]; then
	echo "test8.21.bash <srun_path> <squeue_path> <job_id> <job_size>"
	exit 1
fi
srun=$1
squeue=$2
job_id=$3
job_size=$4

$srun -N1  --test-only /bin/true
sleep 5

while [ $job_size -ge 2 ]
do
	job_size=`expr $job_size / 2`
	$srun -N$job_size --test-only sleep 50 &
	sleep 1
done
$srun -N1  --test-only sleep 50 &
sleep 5
$squeue --jobs=$job_id --steps --noheader --format='Step_ID=%i BP_List=%N'
