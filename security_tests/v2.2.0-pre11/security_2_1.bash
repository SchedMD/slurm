#!/bin/bash
# Define location of slurm executables (if not in default search path)
#slurm_bin="/home/jette/slurm.way/bin/"

# Create private config file
# Set AuthType=auth/dummy
file_orig=`${slurm_bin}scontrol show config | awk '{ if ( $1 ~ /SLURM_CONF/ ) { print $3 } }'`
grep -iv AuthType <$file_orig >tmp.$$
echo "AuthType=auth/dummy" >>tmp.$$

# Run salloc using this config file
export SLURM_CONF=tmp.$$
touch tmp.o.$$
${slurm_bin}salloc /usr/bin/id >> tmp.o.$$

grep --quiet uid tmp.o.$$
if [ "$?" == 1 ] ; then
  echo "salloc errors above are expected"
  echo "SUCCESS"
else
  echo "FAILURE"
fi

# Clean up
rm -f tmp.$$ tmp.o.$$
