#
# Makefile for cray scripts
#

AUTOMAKE_OPTIONS = foreign

EXTRA_DIST = \
	etc_sysconfig_slurm		\
	libalps_test_programs.tar.gz	\
	opt_modulefiles_slurm		\
	pam_job.c

if HAVE_NATIVE_CRAY
sbin_SCRIPTS = slurmconfgen.py
sysconf_DATA = slurm.conf.template
else
EXTRA_DIST += slurmconfgen.py slurm.conf.template
endif
