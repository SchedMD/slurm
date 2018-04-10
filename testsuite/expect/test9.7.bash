#!/usr/bin/env bash
############################################################################
# Simple Slurm stress test
# Usage: <prog> <exec1> <exec2> <exec3> <sleep_time> <iterations>
# Default is sinfo, srun, squeue, 1 second sleep and 3 iterations
############################################################################
# Copyright (C) 2002 The Regents of the University of California.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Morris Jette <jette1@llnl.gov>
# CODE-OCEC-09-009. All rights reserved.
#
# This file is part of Slurm, a resource management program.
# For details, see <https://slurm.schedmd.com/>.
# Please also read the supplied file: DISCLAIMER.
#
# Slurm is free software; you can redistribute it and/or modify it under
# the terms of the GNU General Public License as published by the Free
# Software Foundation; either version 2 of the License, or (at your option)
# any later version.
#
# Slurm is distributed in the hope that it will be useful, but WITHOUT ANY
# WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
# FOR A PARTICULAR PURPOSE.  See the GNU General Public License for more
# details.
#
# You should have received a copy of the GNU General Public License along
# with Slurm; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA.
############################################################################
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
	rc=$?
	if [ $rc -ne 0 ]; then
		echo "exec1 rc=$rc" >> $log
		exit_code=$rc
	fi
	sleep $sleep_time
	$exec2 --job-name=test9.7 -N1-$inx -n$inx -O -s -l -t1 hostname  >>$log 2>&1
	rc=$?
	if [ $rc -ne 0 ]; then
		echo "exec2 rc=$rc" >> $log
		exit_code=$rc
	fi
	sleep $sleep_time
	$exec3                                  >>$log 2>&1
	rc=$?
	if [ $rc -ne 0 ]; then
		echo "exec3 rc=$rc" >> $log
		exit_code=$rc
	fi
	sleep $sleep_time
	inx=$((inx+1))
done

echo "########## EXIT_CODE $exit_code ########## " >>$log 2>&1

if [ $exit_code -ne 0 ]; then
	cat $log
else
	rm $log
fi
echo "########## EXIT_CODE $exit_code ########## "
exit $exit_code
