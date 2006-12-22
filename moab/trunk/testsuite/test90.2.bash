#!/bin/bash 
############################################################################
# Simple Moab stress test
# Usage: <prog> <exec1> <exec2> <exec3> <sleep_time> <iterations>
# Default is mshow, msub, showq, 1 second sleep and 3 iterations
############################################################################
# Copyright (C) 2002-2006 The Regents of the University of California.
# Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
# Written by Morris Jette <jette1@llnl.gov>
############################################################################
if [ $# -gt 0 ]; then
	exec1=$1
else
	exec1="mshow"
fi
if [ $# -gt 1 ]; then
	exec2=$2
else
	exec2="msub"
fi
if [ $# -gt 2 ]; then
	exec3=$3
else
	exec3="showq"
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
log="test90.2.$$.output"
touch $log
while [ $inx -le $iterations ]
do
	echo "########## LOOP $inx ########## " >>$log 2>&1
	$exec1 -a                               >>$log 2>&1
	rc=$?
	if [ $rc -ne 0 ]; then
		echo "exec1 rc=$rc" >> $log
		exit_code=$rc
	fi
	sleep $sleep_time
	$exec2 -l nodes=1 -N test90.2 -o /dev/null -j oe test90.2.input >>$log 2>&1
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
