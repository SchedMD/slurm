#!/bin/bash
#
# Build munge from sources on Cray
#

#----------------------------------------------------------------------------
# CONFIGURATION
#----------------------------------------------------------------------------
# source and build directories
LIBROOT="${LIBROOT:-/ufs/slurm/build}"
MUNGE_BUILD="${LIBROOT}/munge"

# packaging installation directory
DESTDIR="/tmp/munge-build"

# installation and runtime directories
MUNGE_DIR="/opt/slurm/munge"
MUNGE_LOG="/var"

# input and output tarballs
ZIP="${MUNGE_BUILD}/zip"
MUNGE_TAR=${ZIP}/munge*bz2
TARBALL="${LIBROOT}/munge_build-$(date +%F).tar.gz"
#----------------------------------------------------------------------------
# SUBROUTINES
#----------------------------------------------------------------------------
function die() { echo -e "$@" >&2; exit 1; }

function extract_top_level_from_tarball() {
	local tarball="${1:?}" dir
	test -r "${tarball}" || die "can not read ${tarball}"

        case $(file "${tarball}") in
            *gzip*)		compression="-z";;
            *bzip2*)		compression="--bzip2";;
            *compress*data)	compression="--uncompress";;
            *tar*)		compression="";;
	    *)			compression="--auto-compress";;
	esac
	dir="$(tar ${compression} -tf ${tarball} | \
		sed -n '/\// { s@^\([^/]\+\).*$@\1@p;q }')"
	test -n "${dir}" || die "can not determine directory from $tarball"
	echo $dir
}
#----------------------------------------------------------------------------
# SCRIPT PROPER
#----------------------------------------------------------------------------
test ${UID} -eq 0       || die "This script wants to be run by root"
test -d $ZIP		|| die "No tarball directory '$ZIP'"
test -f ${MUNGE_TAR}	|| die "No munge tarball in $ZIP?"
test -d ${LIBROOT}	|| die "Can not cd to LIBROOT=$LIBROOT "
test -d ${MUNGE_BUILD}	|| mkdir -vp ${MUNGE_BUILD}
test -n "${DESTDIR}"    || die "DESTDIR not set"

# generate a clean build directory
rm -rf ${DESTDIR} ${TARBALL}

# DEPENDENT CONFIGURATION
shopt -s nullglob
MUNGE_SRC="${MUNGE_BUILD}/$(extract_top_level_from_tarball ${MUNGE_TAR})" || exit 1
MUNGE_LIB="${DESTDIR}${MUNGE_DIR}/lib"

# extract source
test -d "${LIBROOT}"   || mkdir -vp "${LIBROOT}"
test -d "${MUNGE_SRC}" || tar jxvf ${MUNGE_TAR} -C ${MUNGE_BUILD}
test -d "${MUNGE_SRC}" || die "need to extract munge tarball"
cd ${MUNGE_SRC}

# Build
set -e
./configure --prefix=${MUNGE_DIR} --localstatedir=${MUNGE_LOG}

make -j

mkdir -p ${DESTDIR}
make DESTDIR=${DESTDIR%/}/ install

# final tarball
tar -C ${DESTDIR} -zcpPvf ${TARBALL} .${MUNGE_DIR%/}
# scp ${TARBALL} boot:
echo generated output tarball ${TARBALL}
