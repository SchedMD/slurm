#!/usr/bin/env bash
#
# It is assumed this script runs on cpu $1
# 
# Assumptions for embedded values.
# o the running slurm is idle
# o TaskPlugin=task/cgroup

if test -f "/sys/devices/system/cpu/cpu$1/cpufreq/scaling_governor"
	then
		echo "scaling frequency is supported"
	else
		echo "scaling frequency not supported"
		exit 0
fi
		
sleep 15
smin=$(cat /sys/devices/system/cpu/cpu$1/cpufreq/scaling_min_freq)
scur=$(cat /sys/devices/system/cpu/cpu$1/cpufreq/scaling_cur_freq)
smax=$(cat /sys/devices/system/cpu/cpu$1/cpufreq/scaling_max_freq)
sgov=$(cat /sys/devices/system/cpu/cpu$1/cpufreq/scaling_governor)
govs=$(cat /sys/devices/system/cpu/cpu$1/cpufreq/scaling_available_governors)
freqs=$(cat /sys/devices/system/cpu/cpu$1/cpufreq/scaling_available_frequencies)

echo "available_governors $govs"
echo "available_frequencies $freqs"
echo "scaling_values: gov=$sgov min=$smin cur=$scur max=$smax"
