SUBDIRS = common api db_api database \
	slurmctld slurmd slurmdbd plugins sbcast \
	scontrol scancel squeue sinfo smap sview salloc \
	sbatch sattach strigger sacct sacctmgr sreport sstat \
	sshare sprio

if !BUILD_SRUN2APRUN
if !BG_L_P_LOADED
SUBDIRS += srun
endif
endif

if WITH_BLCR
SUBDIRS += srun_cr
endif



