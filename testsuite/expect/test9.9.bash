#!/bin/bash

if [ $# != 4 ]
then
	echo "Usage: test9.9.bash <sbatch> <prog> <name> <iterations>"
	exit 1
fi

sbatch=$1
prog=$2
job_name=$3
job_cnt=$4
inx=0
iterations=$[job_cnt/10]
while [ $inx -lt $iterations ]
do
	$sbatch -J $job_name -o /dev/null --wrap $prog &
	$sbatch -J $job_name -o /dev/null --wrap $prog &
	$sbatch -J $job_name -o /dev/null --wrap $prog &
	$sbatch -J $job_name -o /dev/null --wrap $prog &
	$sbatch -J $job_name -o /dev/null --wrap $prog &

	$sbatch -J $job_name -o /dev/null --wrap $prog &
	$sbatch -J $job_name -o /dev/null --wrap $prog &
	$sbatch -J $job_name -o /dev/null --wrap $prog &
	$sbatch -J $job_name -o /dev/null --wrap $prog &
	$sbatch -J $job_name -o /dev/null --wrap $prog &
	wait
	inx=$((inx+1))
done

inx=0
iterations=$[job_cnt%10]
while [ $inx -lt $iterations ]
do
	$sbatch -J $job_name -o /dev/null --wrap $prog
	inx=$((inx+1))
done
exit 0
