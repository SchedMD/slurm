if WITH_BLCR
SRUN_CR = srun_cr
else
SRUN_CR =
endif

if BUILD_SRUN2APRUN
SRUN =
else
SRUN = srun
endif

SUBDIRS = common api db_api database \
	slurmctld slurmd slurmdbd plugins sbcast \
	scontrol scancel squeue sinfo smap sview salloc \
	sbatch sattach strigger sacct sacctmgr sreport sstat \
	sshare sprio $(SRUN) $(SRUN_CR)



