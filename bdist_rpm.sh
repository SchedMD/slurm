#!/bin/bash

VERSION=`grep "Version:.*[0-9]" slurm.spec | tr -s " " |  awk '{print $2;}'`
RELEASE=`grep "%global rel.*[-1-9]" slurm.spec | tr -s " " | awk '{print $3}'`

if [ ${RELEASE} -gt 1 ]; then
    SUFFIX=${VERSION}-${RELEASE}
else
    SUFFIX=${VERSION}
fi

git archive --format=tar.gz -o ${HOME}/rpmbuild/SOURCES/slurm-${SUFFIX}.tar.gz --prefix=slurm-${SUFFIX}/ 17.11.ug
cp slurm.spec ${HOME}/rpmbuild/SPECS
rpmbuild -ba ${HOME}/rpmbuild/SPECS/slurm.spec --with=mysql --with=lua --with=hwloc --with=numa
