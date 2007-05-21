Summary: Argonne National Laboratory MPI implementation
Name: mpich
Version: 1.2.7
Vendor: Argonne National Laboratory
Packager: William Gropp <gropp@mcs.anl.gov>
Copyright: BSD-like
Group: System Environment/Libraries 
URL: http://www.mcs.anl.gov/mpi/mpich/
#
# This spec file was inspired by spec files created by others for MPICH,
# including the Scyld version, written by Frederick (Rick) Niles of Scyld, and 
# included as mpich-scyld.spec in the MPICH source distribution.
#
# Construct the source RPM with
#  mkdir ...
#  rpm -ba mpich.spec ....
# This spec file requires a suitably recent version of RPM. Is there an
# RPM version tag that we should set?

# The first part of this spec file parameterizes the choice of compiler
# It is possible to configure and build MPICH for multiple compilers without 
# requiring a new build for each combination of C, Fortran, and C++ compilers.
# This is covered in the installation and users manual; this spec file 
# only handles the case of a single choice of compiler
# Choose only one compiler
%define gnu        1
%define absoft     0
%define intel      0
%define intelia64  0
%define pgi        0
%define lahey      0
%define nag        0

#
# Pick the device.  The ch_p4mpd device is recommended for clusters of
# uniprocessors; the ch_p4 device with comm=shared is appropriate for
# clusters of SMPs.  
%define device ch_p4mpd
%define other_device_opts %{nil}
# (warning: you cannot comment out a macro definition because they
# are apparently evaluated *before* comment processing (!!!)
# for ch_p4:
# define device ch_p4
# define other_device_opts -comm=shared
# You may also want to set
# define rshcommand /usr/bin/ssh
# define rcpcommand /usr/bin/rcp
# for ch_p4 with bproc (see mpich-scyld.spec instead, but the following is
# a start:)
# define device ch_p4
# define other_device_opts -comm=bproc
# for ch_shmem:
# define device ch_shmem
# define other_device_opts %{nil}

#
# Define this to +pvfs if pvfs is available.
%define other_file_systems %{nil}

#
# Define any other options for configure.  For example, 
# Turn off mpe
# define other_config_opts --without-mpe
# Turn off building the logfile viewers
# define other_config_opts -mpe-opts=--disable-viewers
%define other_config_opts %{nil}

#
# Define the release value; this will be used, along with the compiler
# choice, to specify an actual Release name
%define rel 1b

#
# Define whether to install the html and ps/pdf documentation
%define extradoc 1

#
# Set update_paths to 1 when building an RPM for distribution.  Set it
# to 0 when building a test RPM that you want to tryout when a BUILDRROOT
# is specified (if update_paths is 1, then the MPICH commands that need
# to know the file locations are edited to remove the BUILDROOT).
%define update_paths 1
#
# These definitions are approximate.  They are taken in part from the
# mpich-scyld.spec file, though the choices here rely more on the MPICH
# configure to find the correct options for the compilers.  Not all have
# been tested (we don't have access to the Lahey compiler, for example).
%if %{gnu}
    %define c_compiler gcc
    %define compiler_path /usr/bin
    %define setenvs export FC=%{compiler_path}/g77
    %define fopts --disable-f90modules 
    %define extralibs %{nil}
    %define extraldopts %{nil}
    %define release %{rel}
%endif
%if %{absoft}
    %define c_compiler gcc
    %define compiler_path /usr/absoft
    %define setenvs export ABSOFT=%{compiler_path}; export FC=%{compiler_path}/bin/f77; export F90=%{compiler_path}/bin/f90
    %define fopts --without-mpe
    %define extralibs -L%{compiler_path}/lib -lU77 -lf77math
    %define extraldopts %{extralibs}
    %define release %{rel}.absoft
%endif
%if %{intel}
    # Current Intel release is version 7
    %define c_compiler icc
    %define compiler_path /opt/intel/compiler70/ia32
    %define setenvs export PATH=$PATH:%{compiler_path}/bin; export LD_LIBRARY_PATH=%{compiler_path}/lib; export FC=%{compiler_path}/bin/ifc; export F90=$FC
    %define fopts -fflags="-Vaxlib "
    %define extralibs %{nil}
    %define extraldopts -L%{compiler_path}/lib -lPEPCF90 -lIEPCF90 -lF90 %{compiler_path}/lib/libintrins.a
    %define release %{rel}.intel
%endif
%if %{intelia64}
    # Current Intel release is version 7
    # UNTESTED, BASED ON intel for IA32
    %define c_compiler ecc
    %define compiler_path /opt/intel/compiler70/ia64
    %define setenvs export PATH=$PATH:%{compiler_path}/bin; export LD_LIBRARY_PATH=%{compiler_path}/lib; export FC=%{compiler_path}/bin/efc; export F90=$FC
    %define fopts -fflags="-Vaxlib "
    %define extralibs %{nil}
    %define extraldopts -L%{compiler_path}/lib -lPEPCF90 -lIEPCF90 -lF90 %{compiler_path}/lib/libintrins.a
    %define release %{rel}.intel
%endif
%if %{pgi}
    %define c_compiler pgicc
    %define compiler_path /usr/pgi/linux86
    %define setenvs export PATH=$PATH:%{compiler_path}/bin; export PGI=%{compiler_path}/..; export CC="%{compiler_path}/bin/pgcc"; export FC="/usr/pgi/linux86/bin/pgf77"; export F90="%{compiler_path}/bin/pgf90"
    %define fopts %{nil}
    %define extralibs -L%{compiler_path}/lib -lpgc
    %define extraldopts -L%{compiler_path}/lib -lpgc
    %define release %{rel}.pgi
%endif
%if %{lahey}
    %define c_compiler gcc
    %define compiler_path /usr/local/lf9561
    %define setenvs export PATH=%{compiler_path}/bin:$PATH; export LD_LIBRARY_PATH=%{compiler_path}/lib:$LD_LIBRARY_PATH; export FC=%{compiler_path}/bin/lf95; export F90=%{compiler_path}/bin/lf95
    %define fopts %{nil}
    %define extralibs %{nil}
    %define extraldopts %{nil}
    %define release %{rel}.lahey
%endif
%if %{nag}
    %define c_compiler gcc
    %define compiler_path /usr/local
    %define setenvs export FC=%{compiler_path}/bin/f95; export F90=%{compiler_path}/bin/f95
    %define fopts --with-f95nag
    %define extralibs %{nil}
    %define extraldopts %{nil}
    %define release %{rel}.nag
%endif

Release: %{release}
Source0: ftp://ftp.mcs.anl.gov/pub/mpi/%{name}-%{version}.%{rel}.tar.gz
# Next release will include bz2 files
#Source0: ftp://ftp.mcs.anl.gov/pub/mpi/%{name}-%{version}.1.tar.bz2
Buildroot: %{_tmppath}/%{name}-root
#BuildRequires: %{c_compiler}
Provides: libmpich.so.1

# Define alternate root directories for installation here
%define _prefix /usr/local/cca
%define _mandir /usr/local/cca/man
%define _docdir /usr/local/cca/doc

%define fullname %{name}-%{version}

# These directories are defined in terms of the above directories (-prefix 
# and _mandir)  
%define _webdir %{_prefix}/%{fullname}/www

#
# If these are changed, make sure that you change the directories selected 
# under the files step (they must not have the "buildroot" part)
#
# To simplify different installation styles, there are two sets of directory
# definitions.  One uses the default directories (e.g., _libdir) for
# most targets.  The other uses a subdirectory, containing the fullname 
# (e.g., mpich-version), as in %_libdir/%fullname .  
%define use_name_in_dir 0

%if %{use_name_in_dir}
# Choose whether to use a common directory or an mpich one

%define sharedir_inst %{_datadir}/%{fullname}
%define libdir_inst   %{_libdir}/%{fullname}
%define mandir_inst   %{_mandir}/%{fullname}
%define exec_prefix_inst %{_exec_prefix}/%{fullname}
%define includedir_inst %{_includedir}/%{fullname}

%else
# Do *not* include the name of the package in the installation directories

%define sharedir_inst %{_datadir}/%{fullname}
%define libdir_inst   %{_libdir}
%define mandir_inst   %{_mandir}
%define exec_prefix_inst %{_exec_prefix}
%define includedir_inst %{_includedir}

%endif

# For the directories that are defined by their installation dir, make sure 
# that they use the appropriate dire
%define sharedir %{?buildroot:%{buildroot}}%{sharedir_inst}
%define libdir   %{?buildroot:%{buildroot}}%{libdir_inst}
%define mandir   %{?buildroot:%{buildroot}}%{mandir_inst}
%define includedir   %{?buildroot:%{buildroot}}%{includedir_inst}
%define exec_prefix   %{?buildroot:%{buildroot}}%{exec_prefix_inst}

# These directories are the same for either choice
%define prefixdir   %{?buildroot:%{buildroot}}%{_prefix}
%define bindir %{?buildroot:%{buildroot}}%{_bindir}
%define sbindir %{?buildroot:%{buildroot}}%{_sbindir}
%define datadir %{?buildroot:%{buildroot}}%{_datadir}
%define docdir %{?buildroot:%{buildroot}}%{_docdir}
%define webdir %{?buildroot:%{buildroot}}%{_webdir}
%define sharedstatedir %{?buildroot:%{buildroot}}%{_sharedstatedir}

# finalbindir is used to specify the intended installation bindir.  
# Keeping this separate from the bindir allows the use of buildroot.  This
# value is provided to the routines through the --with-rpmbindir=xxx 
# configure option
%define finalbindir %{_bindir}

#package devel
#Summary: Static libraries and header files for MPI.
#Group: Development/Libraries 
#PreReq: %{name} = %{version}

#description devel
#Static libraries and header files for MPI.

%description 
MPICH is an open-source and portable implementation of the Message-Passing
Interface (MPI, www.mpi-forum.org).  MPI is a library for parallel programming,
and is available on a wide range of parallel machines, from single laptops to
massively parallel vector parallel processors.  
MPICH includes all of the routines in MPI 1.2, along with the I/O routines
from MPI-2 and some additional routines from MPI-2, including those supporting
MPI Info and some of the additional datatype constructors.  MPICH  was
developed by Argonne National Laboratory. See www.mcs.anl.gov/mpi/mpich for
more information.

%prep
%setup -q -n mpich-%{version}.%{rel}

#
# The MPICH configure is based on Autoconf version 1, and cannot be rebuilt
# with %%configure.
# These options do the following:
#   Pass the various installation directories into configure
#   Select the MPICH "device"
#   Build the shared libraries, and install them into the same directory
#   as the regular (.a) libraries
#   Build MPI-IO (ROMIO) with the specified file systems, including
#   PVFS if specified by setting other_file_systems to +pvfs.
%build
%setenvs
./configure --prefix=%{prefixdir} \
        --exec-prefix=%{exec_prefix} \
        --bindir=%{bindir} \
	--sbindir=%{sbindir} \
        --datadir=%{datadir}/%{fullname}/ \
        --includedir=%{includedir} \
	--libdir=%{libdir} \
	--docdir=%{docdir} \
        --sharedstatedir=%{sharedstatedir} \
        --mandir=%{mandir} \
	--wwwdir=%{webdir} \
	--with-rpmbindir=%{finalbindir} \
        --with-device=%{device} --enable-sharedlib=%{libdir} \
        %{other_device_opts} %{other_config_opts} \
        --with-romio=--file_system=nfs+ufs%{other_file_systems}
        
%{__make}

%install
%setenvs
if [ $RPM_BUILD_ROOT != "/" ]; then
  rm -rf $RPM_BUILD_ROOT
fi
make install
# Should this be
# make install PREFIX="/.%{prefixdir}" mandir_override=1

# Examples are installed in the wrong location 
mkdir -p %{sharedir}/examples
mv %{prefixdir}/examples/* %{sharedir}/examples

# Remove invalid symlinks
rm -f %{sharedir}/examples/MPI-2-C++/mpirun
rm -f %{sharedir}/examples/mpirun

# Fix the paths in the shell scripts (and ONLY the shell scripts)
# Note that this moves paths into /usr from builddir/fullname
if [ %{update_paths} = 1 ] ; then
for i in `find %{sharedir} %{prefixdir}/bin -type f`
 do
    if (file -b $i | grep ELF >/dev/null) ; then 
        # Ignore binary files.  They must use a search path
        # to allow multiple paths
        :
    else 
        sed 's@%{?buildroot:%{buildroot}}@@g' $i > tmpfile
        sed 's@%{_builddir}/%{name}-%{version}@/usr@g' tmpfile > $i
        if [ -x $i ]; then
            chmod 0755 $i
        else
            chmod 0644 $i
        fi
    fi
 done
fi
%post	-p /sbin/ldconfig

%postun -p /sbin/ldconfig

%clean
if [ $RPM_BUILD_ROOT != "/" ]; then
  rm -rf $RPM_BUILD_ROOT
fi

%files
%defattr(-,root,root)
%doc README COPYRIGHT doc/mpichman-chp4.ps.gz doc/mpichman-chp4mpd.ps.gz
%doc doc/mpichman-chshmem.ps.gz doc/mpeman.ps.gz 
%doc doc/mpeman.pdf doc/mpeman.ps.gz doc/mpiman.ps
%doc doc/mpichman-chp4.pdf doc/mpichman-chp4mpd.pdf 
%doc doc/mpichman-chshmem.pdf 
%doc doc/mpichman-globus2.pdf doc/mpichman-globus2.ps.gz
%{_bindir}/*
%{_sbindir}/*
%if %{extradoc}
%{_webdir}/*
%endif
%{_docdir}/*
%{libdir_inst}/*.so.*
%{libdir_inst}/*.a
%{libdir_inst}/*.so
%{libdir_inst}/mpe_prof.o
%{mandir_inst}/man1/*
%{mandir_inst}/man3/*
%{mandir_inst}/man4/*
%{mandir_inst}/mandesc
%{_datadir}/%{fullname}
%{_datadir}/upshot
%{_datadir}/jumpshot-3
%{_datadir}/jumpshot-2
#%{exec_prefix_inst}/*
%{includedir_inst}/*


%changelog
* Wed Jul 28 2004 William Gropp <gropp@mcs.anl.gov>
- Update version number for next release
* Mon Jan 20 2003 William Gropp <gropp@mcs.anl.gov>
- Integrate suggestions; clean up the code for changing the installation dirs
* Fri Jan 10 2003 William Gropp <gropp@mcs.anl.gov>
- Update to MPICH 1.2.5 and fixed buildroot
* Thu Apr 25 2002 William Gropp <gropp@mcs.anl.gov>
- Initial version
