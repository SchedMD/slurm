#!/bin/bash
# Define location of slurm executables (if not in default search path)
#slurm_bin="/home/jette/slurm.way/bin/"

# Find UID of SlurmUser
slurm_uid=`${slurm_bin}scontrol show config | awk '{ if ( $1 ~ /SlurmUser/ ) { i1 = index($3,"("); i2 = index($3,")"); print substr($3,i1+1,i2-i1-1) } }' `

# Find this UID/GID in /etc/passwd
rm -f tmp.$$
grep ":${slurm_uid}:${slurm_uid}:" /etc/passwd >tmp.$$
cat tmp.$$

# Make sure there is just one entry
wc --lines tmp.$$ | awk '{ if ( $1 == 1 ) { print "SUCCESS" } } '
wc --lines tmp.$$ | awk '{ if ( $1 != 1 ) { print "FAILURE" } } '

# Clean up
rm -f tmp.$$
