#
# Makefile for cray scripts
#

AUTOMAKE_OPTIONS = foreign

EXTRA_DIST = \
	etc_sysconfig_slurm		\
	libalps_test_programs.tar.gz	\
	opt_modulefiles_slurm.in	\
	pam_job.c 			\
	slurm.conf.template		\
	slurmconfgen.py.in

if HAVE_NATIVE_CRAY
sbin_SCRIPTS = slurmconfgen.py
noinst_DATA = opt_modulefiles_slurm
endif

if HAVE_CRAY_NETWORK
noinst_DATA = opt_modulefiles_slurm
endif

# Don't rely on autoconf to replace variables outside of makefiles
opt_modulefiles_slurm: opt_modulefiles_slurm.in Makefile
	sed -e 's|@prefix[@]|$(prefix)|g' \
	    -e 's|@MUNGE_DIR[@]|$(MUNGE_DIR)|g' \
	    -e 's|@libdir[@]|$(libdir)|g' \
	    opt_modulefiles_slurm.in >opt_modulefiles_slurm

slurmconfgen.py: slurmconfgen.py.in Makefile
	sed -e 's|@sysconfdir[@]|$(sysconfdir)|g' \
	    slurmconfgen.py.in >slurmconfgen.py
