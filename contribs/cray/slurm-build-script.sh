#!/bin/bash
#
# Build script for slurm on Cray XT/XE
#
#-------------------------------------------------------------------------------
# CONFIGURATION
#-------------------------------------------------------------------------------
#REBUILD="true" 	# remuild (no distclean/configure)

# source and build directories
LIBROOT="${LIBROOT:-/ufs/slurm/build}"
SLURM_SRC="${SLURM_SRC:-${LIBROOT}/slurm-2.3.0-0.pre4}"

BUILD_ERR="make.err"	# make: stderr only
BUILD_LOG="make.log"	# make: stdout + stderr

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# installation
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# packaging installation directory
DESTDIR="/tmp/slurm-build"

# installation directory
SLURM_ROOT="/opt/slurm"

# symlink to current version
SLURM_DEFAULT="${SLURM_ROOT}/default"

# separate system configuration directory
SLURM_CONF="${SLURM_DEFAULT}/etc"

# space-separated list of things to be built in the contribs/ folder
SLURM_CONTRIBS="contribs/perlapi contribs/torque"
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# dependencies
#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
# path to 'mysql_config' (will be overridden if mysql_config is in $PATH)
MYSQLCONF="${MYSQLCONF:-${LIBROOT}/mysql}"

# munge installation directory containing lib/ and include/ subdirectories
MUNGE_DIR="${SLURM_ROOT}/munge"

#~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

#-------------------------------------------------------------------------------
# SUBROUTINES
#-------------------------------------------------------------------------------
function die() { echo -e "$@">&2; exit -1; }

function get_slurm_version() {
	local vers_file="META"
	if ! test -f $vers_file; then
		die "ERROR: no version file '$vers_file'"\
		    "\nRun this script from within the slurm source directory"
	fi
	sed -n 's/^.*Version:[^0-9]*\([0-9\.]\+\).*$/\1/p' ${vers_file}
}

#-------------------------------------------------------------------------------
# SCRIPT PROPER
#-------------------------------------------------------------------------------
shopt -u nullglob
test ${UID} -eq 0 	|| die "This script wants to be run by root"
test -d ${SLURM_SRC}	|| die "can not cd to SLURM_SRC=$SLURM_SRC"
test -d $MUNGE_DIR/lib	|| die "munge is not yet installed"
test -d ${LIBROOT}	|| die "can not cd to LIBROOT=$LIBROOT"
test -n "${DESTDIR}"    || die "DESTDIR not set"

#-------------------------------------------------------------------
# Dependent Configuration
#-------------------------------------------------------------------
cd ${SLURM_SRC}

# get current slurm version
SLURM_VER=$(get_slurm_version) || die "check your PWD (current: $(pwd))"
SLURM_DIR="${SLURM_ROOT}/${SLURM_VER}"

# name of the tarball to generate at the end of the build process
TARBALL="${LIBROOT}/slurm_build-${SLURM_VER}.tar.gz"
#-------------------------------------------------------------------
# Dependent Tests
#-------------------------------------------------------------------
MYSQL_CONFIG="$(which mysql_config 2>/dev/null)"
if test -z "$MYSQL_CONFIG" -a -z "$MYSQLCONF"; then
	die 'no mysql_config in $PATH - set $MYSQLCONF manually'
elif test -n "$MYSQL_CONFIG"; then
	MYSQLCONF="$(dirname ${MYSQL_CONFIG})"
fi

# generate a clean build directory
rm -rf ${DESTDIR} ${TARBALL}
rm -f  ${BUILD_ERR} ${BUILD_LOG}

# (re)configure
if test -z "${REBUILD}"; then
	set -x
	# clean everything else
	make -j distclean &>/dev/null

	./configure			\
	--prefix="${SLURM_DIR}"		\
	--sysconfdir="${SLURM_CONF}"	\
	--enable-debug 			\
	--enable-front-end\
	--enable-memory-leak-debug	\
	--with-mysql_config=${MYSQLCONF}\
	--with-munge="${MUNGE_DIR}"	\
	--with-hwloc="${HWLOC_DIR}"	\
		|| die "configure failed"
else
	# avoid the slow reconfiguration process, don't build extras
	unset SLURM_CONTRIBS
	touch -r config.status configure config.* configure.ac  Mak*
fi

# Build
tail -F ${BUILD_LOG} & TAIL_PID=$!
set -ex

# swap stderr, stdout, redirect errors in separate, and both into log file
(make -j 3>&1  1>&2  2>&3 | tee ${BUILD_ERR})  &>${BUILD_LOG}
kill ${TAIL_PID} 2>/dev/null
test -s ${BUILD_ERR} &&	cat ${BUILD_ERR} >&2

# Installation
mkdir -p ${DESTDIR}
make -j DESTDIR=${DESTDIR%/}/ install

if false;then
# Perl-API and wrappers for qsub/qstat etc.
for CONTRIB in ${SLURM_CONTRIBS}
do
	test -n "${REBUILD}" || make -C ${CONTRIB} clean
	make -C ${CONTRIB}
	make -C ${CONTRIB} DESTDIR=${DESTDIR%/} install
done
fi

# create the default symlink
rm -vf ${DESTDIR}${SLURM_DEFAULT}
ln -s ${SLURM_VER} ${DESTDIR}${SLURM_DEFAULT}

# Synchronize sources or generate tarball.
tar -C ${DESTDIR} -zcf ${TARBALL} .${SLURM_ROOT} && scp ${TARBALL} boot:
