#!/bin/sh

echo "#!/bin/sh"     >tmp2.$$
echo "id"           >>tmp2.$$

# Run srun using this config file
${slurm_bin}sbatch tmp2.$$

# Clean up
rm -f tmp2.$$
