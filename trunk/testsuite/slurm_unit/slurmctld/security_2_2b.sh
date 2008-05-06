#!/bin/sh
# Define location of slurm executables (if not in default search path)
#slurm_bin="/home/jette/slurm.way/bin/"

# Create private config file
# Set AuthType=auth/dummy
file_orig=`${slurm_bin}scontrol show config | awk '{ if ( $1 ~ /SLURM_CONFIG_FILE/ ) { print $3 } }'`
grep -iv AuthType <$file_orig >tmp.$$
echo "AuthType=auth/dummy" >>tmp.$$

echo "#!/bin/sh"     >tmp2.$$
echo "id"           >>tmp2.$$

# Run srun using this config file
export SLURM_CONF=tmp.$$ 
${slurm_bin}sbatch tmp2.$$

# Clean up
rm -f tmp.$$ tmp2.$$
