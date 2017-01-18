#!/usr/bin/env bash
############################################################################
# Portion of Slurm test suite
############################################################################
# Copyright (C) 2015 SchedMD LLC
# Written by Nathan Yee, SchedMD
#
# This file is part of SLURM, a resource management program.
# For details, see <https://slurm.schedmd.com/>.
# Please also read the included file: DISCLAIMER.
#
# SLURM is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# SLURM is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with SLURM; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
############################################################################
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
	$srun -N1  --test-only --immediate true
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
