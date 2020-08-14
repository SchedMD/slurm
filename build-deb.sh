#!/bin/bash

set -eux

pkg_arch=amd64
pkg_dir=$(realpath ./slurm-${PKG_VERSION}-${pkg_arch})

cd /usr/src/app/slurm-${PKG_VERSION}
./configure --prefix=/usr/lib/x86_64-linux-gnu --libdir=/usr/lib/x86_64-linux-gnu --sysconfdir=/etc/slurm-llnl --bindir=/usr/bin --sbindir=/usr/sbin --includedir=/usr/include --datarootdir=/usr/share
make
make install DESTDIR=$pkg_dir

mkdir $pkg_dir/DEBIAN
cat << EOF > $pkg_dir/DEBIAN/control
Package: slurm
Version: ${PKG_VERSION}
Architecture: ${pkg_arch}
Maintainer: Marshall Adrian <marshall.adrian@sifive.com>
Description: Slurm for SiFive
EOF

dpkg-deb --build $pkg_dir
