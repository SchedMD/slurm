Summary: Argonne National Laboratory MPI implementation
Name: mpich
Version: 1.2.7
Vendor: Scyld Computing Corp.
Distribution: Scyld Beowulf
Packager: Rick Niles <niles@scyld.com>
Copyright: BSD
Group: System Environment/Libraries 

# Choose only one compiler
%define gnu    1
%define absoft 0
%define intel  0
%define pgi    0
%define lahey  0
%define nag    0

%define rel 1

%if %{gnu}
    %define compiler_path /usr/bin
    %define setenvs export FC=%{compiler_path}/g77
    %define fopts --disable-f90modules 
    %define extralibs -Wl,--undefined=beowulf_sched_shim,--undefined=get_beowulf_job_map 
    %define extraldopts %{nil}
    %define release %{rel}
%endif
%if %{absoft}
    %define compiler_path /usr/absoft
    %define setenvs export ABSOFT=%{compiler_path}; export FC=%{compiler_path}/bin/f77; export F90=%{compiler_path}/bin/f90
    %define fopts --without-mpe
    %define extralibs -L%{compiler_path}/lib -lU77 -lf77math
    %define extraldopts %{extralibs}
    %define release %{rel}.absoft
%endif
%if %{intel}
    %define compiler_path /opt/intel/compiler50/ia32
    %define setenvs export PATH=$PATH:%{compiler_path}/bin; export LD_LIBRARY_PATH=%{compiler_path}/lib; export FC=%{compiler_path}/bin/ifc; export F90=$FC
    %define fopts -fflags="-Vaxlib -Qoption,link,--undefined=beowulf_sched_shim,--undefined=get_beowulf_job_map "
    %define extralibs %{nil}
    %define extraldopts -L%{compiler_path}/lib -lPEPCF90 -lIEPCF90 -lF90 %{compiler_path}/lib/libintrins.a
    %define release %{rel}.intel
%endif
%if %{pgi}
    %define compiler_path /usr/pgi/linux86
    %define setenvs export PATH=$PATH:%{compiler_path}/bin; export PGI=%{compiler_path}/..; export CC="%{compiler_path}/bin/pgcc"; export FC="/usr/pgi/linux86/bin/pgf77"; export F90="%{compiler_path}/bin/pgf90"
    %define fopts %{nil}
    %define extralibs -L%{compiler_path}/lib -lpgc
    %define extraldopts -L%{compiler_path}/lib -lpgc
    %define release %{rel}.pgi
%endif
%if %{lahey}
    %define compiler_path /usr/local/lf9561
    %define setenvs export PATH=%{compiler_path}/bin:$PATH; export LD_LIBRARY_PATH=%{compiler_path}/lib:$LD_LIBRARY_PATH; export FC=%{compiler_path}/bin/lf95; export F90=%{compiler_path}/bin/lf95
    %define fopts %{nil}
    %define extralibs %{nil}
    %define extraldopts %{nil}
    %define release %{rel}.lahey
%endif
%if %{nag}
    %define compiler_path /usr/local
    %define setenvs export FC=%{compiler_path}/bin/f95; export F90=%{compiler_path}/bin/f95
    %define fopts --with-f95nag
    %define extralibs %{nil}
    %define extraldopts %{nil}
    %define release %{rel}.nag
%endif

Release: %{release}
Source0: ftp://ftp.mcs.anl.gov/pub/mpi/%{name}-%{version}.tar.gz
Source1: README.SCYLD
Buildroot: %{_tmppath}/%{name}-root
BuildRequires: bproc-devel bproc-libs
BuildRequires: pvfs pvfs-devel beomap
Requires: bproc-libs
Requires: pvfs beomap
Obsoletes: beompi
Obsoletes: npr < 0.2.0
Obsoletes: mpprun
Provides: libmpi.so.1
Provides: libmpich.so.1

%define prefixdir   %{?buildroot:%{buildroot}}%{_prefix}
%define sharedir %{?buildroot:%{buildroot}}%{_datadir}/%{name}
%define libdir   %{?buildroot:%{buildroot}}%{_libdir}/%{name}

%package devel
Summary: Static libraries and header files for MPI.
Group: Development/Libraries 
Obsoletes: beompi-devel
PreReq: %{name} = %{version}

%description devel
Static libraries and header files for MPI.

%description
An implementation of the Message-Passing Interface (MPI) by Argonne
National Laboratory.  This version has been configured to work with
Scyld Beowulf.

%prep
%setup -q -n mpich

# Set the right flags for Scyld Beowulf
#    -e 's/rcpcmd=${RCPCOMMAND-rcp}/rcpcmd=bpcp/' \
sed -e 's/move_pgfile_to_master=no/move_pgfile_to_master=yes/' \
    mpid/ch_p4/mpirun.ch_p4.in > tmp.scyld
mv tmp.scyld mpid/ch_p4/mpirun.ch_p4.in
sed -e "s@external_allocate_cmd=\"\"@external_allocate_cmd=\"/usr/bin/beomap | sed 's/:/ /g'\"@" util/mpirun.pg.in > tmp.scyld
mv tmp.scyld util/mpirun.pg.in
cp -a %{SOURCE1} .

%build
%setenvs
export RSHCOMMAND=/usr/bin/bpsh
export RCPCOMMAND=/usr/bin/bpcp
./configure --with-arch=LINUX --enable-sharedlib \
	    %{fopts} \
            --with-comm=bproc  \
	    --with-romio=--file_system=nfs+ufs+pvfs \
	    --lib="%{extralibs} -lbproc -lpvfs -lbeomap -lbeostat -ldl" \
	    --with-device=ch_p4

%{__make}

# Right now the shared libs build by mpich don't do Fortran, 
#  so re-link here.
# Add in FORTRAN stub so it will be easy to build shared C library
cat > fortran-stub.c <<EOF
void getarg_ (long *n, char *s, short ls) __attribute__ ((weak));
void getarg_ (long *n, char *s, short ls) {}
int f__xargc __attribute__ ((weak)) = -1;
%if %{intel}
int xargc __attribute__ ((weak)) = -1;
%endif
%if %{lahey} || %{nag}
int iargc_ __attribute__ ((weak)) = -1;
%endif
EOF
gcc -O -c fortran-stub.c
ar rs lib/libmpich.a fortran-stub.o
rm -f fortran-stub.[co]

# This relinking is only necessary since the Fortran needs to be made to work.
%if ! %{pgi}
for i in mpich fmpich pmpich
  do
    ld -shared -soname lib${i}.so.1 --whole-archive lib/lib${i}.a --no-whole-archive -u beowulf_sched_shim -u get_beowulf_job_map -lbeomap -lbproc -lbeostat -lminipvfs -ldl -lc %{extraldopts} -o lib/shared/lib${i}.so.1.0 
  done

# Make bogus "libmpi.so" and "libmpi.so.1"
pushd lib/shared
cat > libmpi.so <<EOF 
/* GNU ld script 
   This should point to the shared library of the default MPI
   implementation.  This file is only used at compile time.   */
INPUT ( /usr/lib/libmpich.so.1.0 )
EOF
popd
%endif

# Fix mpif77/90 missing user flags mistake
%if %{intel}
for i in bin/mpif77 bin/mpif90 src/fortran/src/mpif77 src/fortran/src/mpif90 src/fortran/src/mpif77.in src/fortran/src/mpif90.in
do
  sed 's/$BASE_FFLAGS/$BASE_FFLAGS $USER_FFLAGS/g' $i > tmp1
  mv tmp1 $i
  chmod +x $i
done
%endif

%install
%setenvs
export RSHCOMMAND=/usr/bin/bash
if [ $RPM_BUILD_ROOT != "/" ]; then
  rm -rf $RPM_BUILD_ROOT
fi
make PREFIX=%{prefixdir} install

# The default share dir is "/usr/mpich/share" should be "/usr/share/mpich"
mv %{prefixdir}/share %{prefixdir}/share-old
mkdir -p %{sharedir}
mv %{prefixdir}/share-old/* %{sharedir} 
mkdir -p %{sharedir}/examples
mv %{prefixdir}/examples/* %{sharedir}/examples

%if ! %{pgi}
# Move the shared libs to the main lib dir
mv %{prefixdir}/lib/shared/* %{prefixdir}/lib

# Make sym. link for libmpi.a and libmpi.so.1, create libmpi.so linker script
pushd %{prefixdir}/lib
ln -s libmpich.a libmpi.a
ln -s libmpich.so.1 libmpi.so.1
popd
%endif

# Remove invalid symlinks
rm -f %{sharedir}/examples/MPI-2-C++/mpirun
rm -f %{sharedir}/examples/mpirun

# Fix the paths in the shell scripts
for i in `find %{sharedir} %{prefixdir}/bin -type f`
 do
    sed 's@%{?buildroot:%{buildroot}}@@g' $i > tmpfile
    sed 's@%{_builddir}/%{name}-%{version}@/usr@g' tmpfile > $i
    if [ -x $i ]; then
        chmod 0755 $i
    else
        chmod 0644 $i
    fi
 done

%post	-p /sbin/ldconfig

%postun -p /sbin/ldconfig

%clean
if [ $RPM_BUILD_ROOT != "/" ]; then
  rm -rf $RPM_BUILD_ROOT
fi

%files
%defattr(-,root,root)
%doc README.SCYLD README COPYRIGHT doc/guide.ps.gz doc/install.ps.gz
/usr/bin/*
#/usr/sbin/*  # doesn't seem to have anything useful for us.
%if ! %{pgi}
/usr/lib/*.so.*
%endif
/usr/man/man1/*

%files devel
%defattr(-,root,root)
/usr/share/*
/usr/include/*
/usr/lib/*.a
%if ! %{pgi}
/usr/lib/*.so
%endif
/usr/man/man3/*
/usr/man/man4/*

%changelog
* Mon July 26 2004 William Gropp <gropp@mcs.anl.gov>
- Update for 1.2.6 version number.
* Sat Jan  4 2003 William Gropp <gropp@mcs.anl.gov>
- Update for 1.2.5 version number.
* Thu Apr 26 2001 Frederick (Rick) A Niles <niles@scyld.com>
- Initial version
