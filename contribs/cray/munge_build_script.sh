#!/bin/bash
#
# Build munge from sources on Cray
#

#----------------------------------------------------------------------------
# CONFIGURATION
#----------------------------------------------------------------------------
shopt -s nullglob
# source and build directories
LIBROOT="${LIBROOT:-/ufs/slurm/build}"
MUNGE_BUILD="${LIBROOT}/munge"

# packaging installation directory
DESTDIR="/tmp/munge-build"

# installation and runtime directories
# installation directory for commands and headers
MUNGE_DIR="/opt/slurm/munge"
MUNGE_LOG="/var"
# to avoid problems with truncated LD_LIBRARY_PATH, install libraries in /usr
MUNGE_LIB="/usr/lib64"

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
MUNGE_SRC="${MUNGE_BUILD}/$(extract_top_level_from_tarball ${MUNGE_TAR})" || exit 1

test -d "${MUNGE_SRC}" || tar jxvf ${MUNGE_TAR} -C ${MUNGE_BUILD}
test -d "${MUNGE_SRC}" || die "need to extract munge tarball"

# extract source
cd ${MUNGE_SRC}
set -e
	./configure			\
	--prefix=${MUNGE_DIR}		\
  	--libdir=${MUNGE_LIB}		\
	--localstatedir=${MUNGE_LOG}

# Build
make -j

# Installation
mkdir -p ${DESTDIR}
make DESTDIR=${DESTDIR%/}/ install

# Generate a rudimentary key for testing
MUNGE_ETC="${DESTDIR}${MUNGE_DIR}/etc"
MUNGE_KEY="${MUNGE_ETC}/munge.key"

mkdir -vp $MUNGE_ETC
# dd if=/dev/random  bs=1 count=1024	> $MUNGE_KEY	# true random data (takes ~15 minutes)
dd if=/dev/urandom bs=1 count=1024	> $MUNGE_KEY	# pseudo-random data (faster)
# echo -n "foo"|sha1sum|cut -d' ' -f1	> $MUNGE_KEY	# sha1 hash of 'foo' password
# echo "clear_text_password"		> $MUNGE_KEY	# plaintext password (BAD)
chmod 0700 $MUNGE_ETC
chmod 0400 $MUNGE_KEY

# tarball
tar -C ${DESTDIR} -zcpPvf ${TARBALL} .${MUNGE_DIR%/}/{share,include,bin,sbin,etc} .${MUNGE_LIB}
