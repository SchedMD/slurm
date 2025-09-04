Name:		slurm
Version:	25.05.3
%define rel	1
%if %{defined patch} && %{undefined extraver}
%define extraver .patched
%endif
Release:	%{rel}%{?extraver}%{?dist}
Summary:	Slurm Workload Manager

Group:		System Environment/Base
License:	GPLv2+
URL:		https://slurm.schedmd.com/

# when the rel number is one, the directory name does not include it
%if "%{rel}" == "1"
%global slurm_source_dir %{name}-%{version}
%else
%global slurm_source_dir %{name}-%{version}-%{rel}
%endif

Source:		%{slurm_source_dir}.tar.bz2
%{lua: local patchnum=0
  for pfile in string.gmatch(rpm.expand("%{?patch}"), "%S+") do
    print('Patch'..patchnum..':\t'..pfile..'\n')
    patchnum=patchnum+1
  end
}

# build options		.rpmmacros options	change to default action
# ====================  ====================	========================
# --prefix		%_prefix path		install path for commands, libraries, etc.
# --with cray_shasta	%_with_cray_shasta 1	build for a Cray Shasta system
# --with slurmrestd	%_with_slurmrestd 1	build slurmrestd
# --with yaml		%_with_yaml 1		build with yaml serializer
# --without debug	%_without_debug 1	don't compile with debugging symbols
# --with hdf5		%_with_hdf5 path	require hdf5 support
# --with hwloc		%_with_hwloc 1		require hwloc support
# --with libcurl	%_with_libcurl 1	require libcurl support
# --with lua		%_with_lua path		build Slurm lua bindings
# --without munge	%_without_munge 1	disable support for munge
# --with numa		%_with_numa 1		require NUMA support
# --without pam		%_without_pam 1		don't require pam-devel RPM to be installed
# --without x11		%_without_x11 1		disable internal X11 support
# --with ucx		%_with_ucx path		require ucx support
# --with pmix		%_with_pmix path	build with pmix support
# --with nvml		%_with_nvml path	require nvml support
# --with jwt		%_with_jwt 1		require jwt support
# --with freeipmi	%_with_freeipmi 1	require freeipmi support
# --with selinux	%_with_selinux 1	build with selinux support
#

#  Options that are off by default (enable with --with <opt>)
%bcond_with cray_shasta
%bcond_with slurmrestd
%bcond_with multiple_slurmd
%bcond_with pmix
%bcond_with ucx

# These options are only here to force there to be these on the build.
# If they are not set they will still be compiled if the packages exist.
%bcond_with hwloc
%bcond_with hdf5
%bcond_with libcurl
%bcond_with lua
%bcond_with numa
%bcond_with nvml
%bcond_with jwt
%bcond_with yaml
%bcond_with freeipmi

# Use debug by default on all systems
%bcond_without debug

# Options enabled by default
%bcond_without munge
%bcond_without pam
%bcond_without x11

# Disable hardened builds. -z,now or -z,relro breaks the plugin stack
%undefine _hardened_build
%global _hardened_cflags "-Wl,-z,lazy"
%global _hardened_ldflags "-Wl,-z,lazy"

# Disable Link Time Optimization (LTO)
%define _lto_cflags %{nil}

BuildRequires:  autoconf
BuildRequires:  automake
BuildRequires:  gcc
BuildRequires:  make
%if %{defined suse_version}
BuildRequires: pkg-config
%else
%if (0%{?rhel} != 7)
BuildRequires:  pkgconf
%else
BuildRequires:  pkgconfig
%endif
%endif

%if %{with munge}
Requires: munge
BuildRequires: munge-devel munge-libs
%endif

Requires: bash-completion

%{?systemd_requires}
BuildRequires: systemd
BuildRequires: python3
BuildRequires: readline-devel
Obsoletes: slurm-lua <= %{version}
Obsoletes: slurm-munge <= %{version}
Obsoletes: slurm-plugins <= %{version}

# fake systemd support when building rpms on other platforms
%{!?_unitdir: %global _unitdir /lib/systemd/systemd}

%define use_mysql_devel %(perl -e '`rpm -q mysql-devel`; print !$?;')
# Default for OpenSUSE/SLES builds
%define use_libmariadb_devel %(perl -e '`rpm -q libmariadb-devel`; print !$?;')
# Package name from the official MariaDB version
%define use_MariaDB_devel %(perl -e '`rpm -q MariaDB-devel`; print !$?;')
# Oracle mysql community
%define use_mysql_community %(perl -e '`rpm -q mysql-community-devel`; print !$?;')
# Oracle mysql commercial
%define use_mysql_commercial %(perl -e '`rpm -q mysql-commercial-devel`; print !$?;')

%if 0%{?use_mysql_devel}
BuildRequires: mysql-devel >= 5.0.0
%else
%if 0%{?use_mysql_community}
BuildRequires: mysql-community-devel >= 5.0.0
%else
%if 0%{?use_mysql_commercial}
BuildRequires: mysql-commercial-devel >= 5.0.0
%else
%if 0%{?use_libmariadb_devel}
# OpenSUSE/SLES has a different versioning scheme, so skip the version check
BuildRequires: libmariadb-devel
%else
%if 0%{?use_MariaDB_devel}
BuildRequires: MariaDB-devel >= 5.0.0
%else
BuildRequires: mariadb-devel >= 5.0.0
%endif
%endif
%endif
%endif
%endif

BuildRequires: perl(ExtUtils::MakeMaker)
%if %{defined suse_version}
BuildRequires: perl
%else
BuildRequires: perl-devel
%endif

%if %{with lua}
BuildRequires: pkgconfig(lua) >= 5.1.0
%endif

%if %{with hwloc} && "%{_with_hwloc}" == "--with-hwloc"
BuildRequires: hwloc-devel
%endif

%if %{with numa}
%if %{defined suse_version}
BuildRequires: libnuma-devel
%else
BuildRequires: numactl-devel
%endif
%endif

%if %{with pmix} && "%{_with_pmix}" == "--with-pmix"
BuildRequires: pmix
%global pmix_version %(rpm -q pmix --qf "%{RPMTAG_VERSION}")
%endif

%if %{with ucx} && "%{_with_ucx}" == "--with-ucx"
BuildRequires: ucx-devel
%global ucx_version %(rpm -q ucx-devel --qf "%{RPMTAG_VERSION}")
%endif

%if %{with libcurl}
%if %{defined suse_version}
Requires: libcurl
%else
Requires: libcurl4
%endif
BuildRequires: libcurl-devel
%endif

%if %{with jwt}
BuildRequires: libjwt-devel >= 1.10.0
Requires: libjwt >= 1.10.0
%endif

%if %{with yaml}
Requires: libyaml >= 0.1.7
BuildRequires: libyaml-devel >= 0.1.7
%endif

%if %{with freeipmi}
Requires: freeipmi
BuildRequires: freeipmi-devel
%endif

%if %{with selinux}
Requires: libselinux
BuildRequires: libselinux-devel
%endif

#  Allow override of sysconfdir via _slurm_sysconfdir.
#  Note 'global' instead of 'define' needed here to work around apparent
#   bug in rpm macro scoping (or something...)
%{!?_slurm_sysconfdir: %global _slurm_sysconfdir /etc/slurm}
%define _sysconfdir %_slurm_sysconfdir

#  Allow override of datadir via _slurm_datadir.
%{!?_slurm_datadir: %global _slurm_datadir %{_prefix}/share}
%define _datadir %{_slurm_datadir}

#  Allow override of mandir via _slurm_mandir.
%{!?_slurm_mandir: %global _slurm_mandir %{_datadir}/man}
%define _mandir %{_slurm_mandir}

#  Allow override of bashcompdir via _slurm_bashcompdir.
%{!?_slurm_bashcompdir: %global _slurm_bashcompdir %{_datadir}}
%define _bashcompdir %{_slurm_bashcompdir}

#
# Never allow rpm to strip binaries as this will break
#  parallel debugging capability
# Note that brp-compress does not compress man pages installed
#  into non-standard locations (e.g. /usr/local)
#
%define __os_install_post /usr/lib/rpm/brp-compress
%define debug_package %{nil}

#
# Should unpackaged files in a build root terminate a build?
# Uncomment if needed again.
#%define _unpackaged_files_terminate_build      0

# Slurm may intentionally include empty manifest files, which will
# cause errors with rpm 4.13 and on. Turn that check off.
%define _empty_manifest_terminate_build 0

# First we remove $prefix/local and then just prefix to make
# sure we get the correct installdir
%define _perlarch %(perl -e 'use Config; $T=$Config{installsitearch}; $P=$Config{installprefix}; $P1="$P/local"; $T =~ s/$P1//; $T =~ s/$P//; print $T;')

%define _perlman3 %(perl -e 'use Config; $T=$Config{installsiteman3dir}; $P=$Config{siteprefix}; $P1="$P/local"; $T =~ s/$P1//; $T =~ s/$P//; print $T;')

%define _perlarchlib %(perl -e 'use Config; $T=$Config{installarchlib}; $P=$Config{installprefix}; $P1="$P/local"; $T =~ s/$P1//; $T =~ s/$P//; print $T;')

%define _perldir %{_prefix}%{_perlarch}
%define _perlman3dir %{_prefix}%{_perlman3}
%define _perlarchlibdir %{_prefix}%{_perlarchlib}

%description
Slurm is an open source, fault-tolerant, and highly scalable
cluster management and job scheduling system for Linux clusters.
Components include machine status, partition management,
job management, scheduling and accounting modules

%package perlapi
Summary: Perl API to Slurm
Group: Development/System
Requires: %{name}%{?_isa} = %{version}-%{release}
%description perlapi
Perl API package for Slurm.  This package includes the perl API to provide a
helpful interface to Slurm through Perl

%package devel
Summary: Development package for Slurm
Group: Development/System
Requires: %{name}%{?_isa} = %{version}-%{release}
%description devel
Development package for Slurm.  This package includes the header files
and static libraries for the Slurm API

%package example-configs
Summary: Example config files for Slurm
Group: Development/System
%description example-configs
Example configuration files for Slurm.

%package sackd
Summary: Slurm authentication daemon
Group: System Environment/Base
Requires: %{name}%{?_isa} = %{version}-%{release}
%description sackd
Slurm authentication daemon. Used on login nodes that are not running slurmd
daemons to allow authentication to the cluster.

%package slurmctld
Summary: Slurm controller daemon
Group: System Environment/Base
Requires: %{name}%{?_isa} = %{version}-%{release}
%if %{with pmix} && "%{_with_pmix}" == "--with-pmix"
Requires: pmix = %{pmix_version}
%endif
%if %{with ucx} && "%{_with_ucx}" == "--with-ucx"
Requires: ucx = %{ucx_version}
%endif
%description slurmctld
Slurm controller daemon. Used to manage the job queue, schedule jobs,
and dispatch RPC messages to the slurmd processon the compute nodes
to launch jobs.

%package slurmd
Summary: Slurm compute node daemon
Group: System Environment/Base
Requires: %{name}%{?_isa} = %{version}-%{release}
%if %{with pmix} && "%{_with_pmix}" == "--with-pmix"
Requires: pmix = %{pmix_version}
%endif
%if %{with ucx} && "%{_with_ucx}" == "--with-ucx"
Requires: ucx = %{ucx_version}
%endif
%description slurmd
Slurm compute node daemon. Used to launch jobs on compute nodes

%package slurmdbd
Summary: Slurm database daemon
Group: System Environment/Base
Requires: %{name}%{?_isa} = %{version}-%{release}
Obsoletes: slurm-sql <= %{version}
%description slurmdbd
Slurm database daemon. Used to accept and process database RPCs and upload
database changes to slurmctld daemons on each cluster

%package libpmi
Summary: Slurm\'s implementation of the pmi libraries
Group: System Environment/Base
Requires: %{name}%{?_isa} = %{version}-%{release}
Conflicts: pmix-libpmi
%description libpmi
Slurm\'s version of libpmi. For systems using Slurm, this version
is preferred over the compatibility libraries shipped by the PMIx project.

%package torque
Summary: Torque/PBS wrappers for transition from Torque/PBS to Slurm
Group: Development/System
Requires: slurm-perlapi
%description torque
Torque wrapper scripts used for helping migrate from Torque/PBS to Slurm

%package openlava
Summary: openlava/LSF wrappers for transition from OpenLava/LSF to Slurm
Group: Development/System
Requires: slurm-perlapi
%description openlava
OpenLava wrapper scripts used for helping migrate from OpenLava/LSF to Slurm

%package contribs
Summary: Perl tool to print Slurm job state information
Group: Development/System
Requires: %{name}%{?_isa} = %{version}-%{release}
Obsoletes: slurm-sjobexit <= %{version}
Obsoletes: slurm-sjstat <= %{version}
Obsoletes: slurm-seff <= %{version}
%description contribs
seff is a mail program used directly by the Slurm daemons. On completion of a
job, wait for it's accounting information to be available and include that
information in the email body.
sjobexit is a slurm job exit code management tool. It enables users to alter
job exit code information for completed jobs
sjstat is a Perl tool to print Slurm job state information. The output is designed
to give information on the resource usage and availablilty, as well as information
about jobs that are currently active on the machine. This output is built
using the Slurm utilities, sinfo, squeue and scontrol, the man pages for these
utilities will provide more information and greater depth of understanding.

%if %{with pam}
%package pam_slurm
Summary: PAM module for restricting access to compute nodes via Slurm
Group: System Environment/Base
Requires: %{name}%{?_isa} = %{version}-%{release}
BuildRequires: pam-devel
Obsoletes: pam_slurm <= %{version}
%description pam_slurm
This module restricts access to compute nodes in a cluster where Slurm is in
use.  Access is granted to root, any user with an Slurm-launched job currently
running on the node, or any user who has allocated resources on the node
according to the Slurm
%endif

%if %{with slurmrestd}
%package slurmrestd
Summary: Slurm REST API translator
Group: System Environment/Base
Requires: %{name}%{?_isa} = %{version}-%{release}
BuildRequires: http-parser-devel
%if %{defined suse_version}
BuildRequires: libjson-c-devel
%else
BuildRequires: json-c-devel
%endif
%description slurmrestd
Provides a REST interface to Slurm.
%endif

#############################################################################

%prep
# when the rel number is one, the tarball filename does not include it
%setup -n %{slurm_source_dir}
%global _default_patch_fuzz 2
%autopatch -p1

%build
%configure \
	--with-systemdsystemunitdir=%{_unitdir} \
	--enable-pkgconfig \
	%{?_without_debug:--disable-debug} \
	%{?_with_pam_dir} \
	%{?_with_mysql_config} \
	%{?_with_multiple_slurmd:--enable-multiple-slurmd} \
	%{?_with_selinux:--enable-selinux} \
	%{?_with_pmix} \
	%{?_with_freeipmi} \
	%{?_with_hdf5} \
	%{?_with_hwloc} \
	%{?_with_shared_libslurm} \
	%{!?_with_slurmrestd:--disable-slurmrestd} \
	%{?_without_x11:--disable-x11} \
	%{?_with_libcurl} \
	%{?_with_ucx} \
	%{?_with_jwt} \
	%{?_with_yaml} \
	%{?_with_nvml} \
	%{?_with_freeipmi} \
	%{!?with_munge:--without-munge} \
	%{?_with_cflags}

make %{?_smp_mflags}

%install

# Ignore redundant standard rpaths and insecure relative rpaths,
# for RHEL based distros which use "check-rpaths" tool.
export QA_RPATHS=0x5

# Strip out some dependencies

cat > find-requires.sh <<'EOF'
exec %{__find_requires} "$@" | grep -E -v '^libpmix.so|libevent|libnvidia-ml'
EOF
chmod +x find-requires.sh
%global _use_internal_dependency_generator 0
%global __find_requires %{_builddir}/%{buildsubdir}/find-requires.sh

rm -rf %{buildroot}
make install DESTDIR=%{buildroot}
make install-contrib DESTDIR=%{buildroot}

# Do not package Slurm's version of libpmi on Cray systems in the usual location.
# Cray's version of libpmi should be used. Move it elsewhere if the site still
# wants to use it with other MPI stacks.
%if %{with cray_shasta}
   mkdir %{buildroot}/%{_libdir}/slurmpmi
   mv %{buildroot}/%{_libdir}/libpmi* %{buildroot}/%{_libdir}/slurmpmi
%endif

install -D -m644 etc/cgroup.conf.example %{buildroot}/%{_sysconfdir}/cgroup.conf.example
install -D -m644 etc/prolog.example %{buildroot}/%{_sysconfdir}/prolog.example
install -D -m644 etc/job_submit.lua.example %{buildroot}/%{_sysconfdir}/job_submit.lua.example
install -D -m644 etc/slurm.conf.example %{buildroot}/%{_sysconfdir}/slurm.conf.example
install -D -m600 etc/slurmdbd.conf.example %{buildroot}/%{_sysconfdir}/slurmdbd.conf.example
install -D -m644 etc/cli_filter.lua.example %{buildroot}/%{_sysconfdir}/cli_filter.lua.example
install -D -m755 contribs/sjstat %{buildroot}/%{_bindir}/sjstat

# Delete unpackaged files:
find %{buildroot} -name '*.a' -exec rm {} \;
find %{buildroot} -name '*.la' -exec rm {} \;
rm -f %{buildroot}/%{_libdir}/slurm/job_submit_defaults.so
rm -f %{buildroot}/%{_libdir}/slurm/job_submit_logging.so
rm -f %{buildroot}/%{_libdir}/slurm/job_submit_partition.so
rm -f %{buildroot}/%{_libdir}/slurm/auth_none.so
rm -f %{buildroot}/%{_libdir}/slurm/cred_none.so
rm -f %{buildroot}/%{_sbindir}/sfree
rm -f %{buildroot}/%{_sbindir}/slurm_epilog
rm -f %{buildroot}/%{_sbindir}/slurm_prolog
rm -f %{buildroot}/%{_sysconfdir}/init.d/slurm
rm -f %{buildroot}/%{_sysconfdir}/init.d/slurmdbd
rm -f %{buildroot}/%{_perldir}/auto/Slurm/.packlist
rm -f %{buildroot}/%{_perldir}/auto/Slurm/Slurm.bs
rm -f %{buildroot}/%{_perlarchlibdir}/perllocal.pod
rm -f %{buildroot}/%{_perldir}/perllocal.pod
rm -f %{buildroot}/%{_perldir}/auto/Slurmdb/.packlist
rm -f %{buildroot}/%{_perldir}/auto/Slurmdb/Slurmdb.bs
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/sacct
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/sacctmgr
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/salloc
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/sattach
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/sbatch
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/sbcast
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/scancel
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/scontrol
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/scrontab
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/sdiag
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/sinfo
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/slurmrestd
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/sprio
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/squeue
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/sreport
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/srun
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/sshare
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/sstat
rm -f %{buildroot}/%{_datadir}/bash-completion/completions/strigger

# Build man pages that are generated directly by the tools
rm -f %{buildroot}/%{_mandir}/man1/sjobexitmod.1
%{buildroot}/%{_bindir}/sjobexitmod --roff > %{buildroot}/%{_mandir}/man1/sjobexitmod.1
rm -f %{buildroot}/%{_mandir}/man1/sjstat.1
%{buildroot}/%{_bindir}/sjstat --roff > %{buildroot}/%{_mandir}/man1/sjstat.1

LIST=./pam.files
touch $LIST
%if %{?with_pam_dir}0
    test -f %{buildroot}/%{with_pam_dir}/pam_slurm.so	&&
	echo %{with_pam_dir}/pam_slurm.so	>>$LIST
    test -f %{buildroot}/%{with_pam_dir}/pam_slurm_adopt.so	&&
	echo %{with_pam_dir}/pam_slurm_adopt.so	>>$LIST
%else
    test -f %{buildroot}/lib/security/pam_slurm.so	&&
	echo /lib/security/pam_slurm.so		>>$LIST
    test -f %{buildroot}/lib32/security/pam_slurm.so	&&
	echo /lib32/security/pam_slurm.so	>>$LIST
    test -f %{buildroot}/lib64/security/pam_slurm.so	&&
	echo /lib64/security/pam_slurm.so	>>$LIST
    test -f %{buildroot}/lib/security/pam_slurm_adopt.so		&&
	echo /lib/security/pam_slurm_adopt.so		>>$LIST
    test -f %{buildroot}/lib32/security/pam_slurm_adopt.so		&&
	echo /lib32/security/pam_slurm_adopt.so		>>$LIST
    test -f %{buildroot}/lib64/security/pam_slurm_adopt.so		&&
	echo /lib64/security/pam_slurm_adopt.so		>>$LIST
%endif
#############################################################################

%clean
rm -rf %{buildroot}
#############################################################################

%files
%defattr(-,root,root,0755)
%{_datadir}/doc
%{_bindir}/s*
%exclude %{_bindir}/seff
%exclude %{_bindir}/sjobexitmod
%exclude %{_bindir}/sjstat
%exclude %{_bindir}/smail
%exclude %{_libdir}/libpmi*
%{_libdir}/*.so*
%{_libdir}/slurm/src/*
%{_libdir}/slurm/*.so
%exclude %{_libdir}/slurm/accounting_storage_mysql.so
%exclude %{_libdir}/slurm/job_submit_pbs.so
%exclude %{_libdir}/slurm/spank_pbs.so
%{_mandir}
%exclude %{_mandir}/man1/sjobexit*
%exclude %{_mandir}/man1/sjstat*
%dir %{_libdir}/slurm/src
%{_bashcompdir}/bash-completion/completions/slurm_completion.sh
#############################################################################

%files example-configs
%defattr(-,root,root,0755)
%dir %{_sysconfdir}
%config %{_sysconfdir}/cgroup.conf.example
%config %{_sysconfdir}/job_submit.lua.example
%config %{_sysconfdir}/prolog.example
%config %{_sysconfdir}/slurm.conf.example
%config %{_sysconfdir}/slurmdbd.conf.example
%config %{_sysconfdir}/cli_filter.lua.example
#############################################################################

%files devel
%defattr(-,root,root)
%dir %attr(0755,root,root)
%dir %{_prefix}/include/slurm
%{_prefix}/include/slurm/*
%{_libdir}/pkgconfig/slurm.pc
#############################################################################

%files perlapi
%defattr(-,root,root)
%{_perldir}/Slurm.pm
%{_perldir}/Slurm/Bitstr.pm
%{_perldir}/Slurm/Constant.pm
%{_perldir}/Slurm/Hostlist.pm
%{_perldir}/auto/Slurm/Slurm.so
%{_perldir}/Slurmdb.pm
%{_perldir}/auto/Slurmdb/Slurmdb.so
%{_perldir}/auto/Slurmdb/autosplit.ix
%{_perlman3dir}/Slurm*

#############################################################################

%files sackd
%defattr(-,root,root)
%{_sbindir}/sackd
%{_unitdir}/sackd.service
#############################################################################

%files slurmctld
%defattr(-,root,root)
%{_sbindir}/slurmctld
%{_unitdir}/slurmctld.service
#############################################################################

%files slurmd
%defattr(-,root,root)
%{_sbindir}/slurmd
%{_sbindir}/slurmstepd
%{_unitdir}/slurmd.service
#############################################################################

%files slurmdbd
%defattr(-,root,root)
%{_sbindir}/slurmdbd
%{_libdir}/slurm/accounting_storage_mysql.so
%{_unitdir}/slurmdbd.service
#############################################################################

%files libpmi
%defattr(-,root,root)
%if %{with cray_shasta}
%{_libdir}/slurmpmi/*
%else
%{_libdir}/libpmi*
%endif
#############################################################################

%files torque
%defattr(-,root,root)
%{_bindir}/pbsnodes
%{_bindir}/qalter
%{_bindir}/qdel
%{_bindir}/qhold
%{_bindir}/qrerun
%{_bindir}/qrls
%{_bindir}/qstat
%{_bindir}/qsub
%{_bindir}/mpiexec
%{_bindir}/generate_pbs_nodefile
%{_libdir}/slurm/job_submit_pbs.so
%{_libdir}/slurm/spank_pbs.so
#############################################################################

%files openlava
%defattr(-,root,root)
%{_bindir}/bjobs
%{_bindir}/bkill
%{_bindir}/bsub
%{_bindir}/lsid

#############################################################################

%files contribs
%defattr(-,root,root)
%{_bindir}/seff
%{_bindir}/sjobexitmod
%{_bindir}/sjstat
%{_bindir}/smail
%{_mandir}/man1/sjstat*
#############################################################################

%if %{with pam}
%files -f pam.files pam_slurm
%defattr(-,root,root)
%endif
#############################################################################

%if %{with slurmrestd}
%files slurmrestd
%{_sbindir}/slurmrestd
%{_unitdir}/slurmrestd.service
%endif
#############################################################################

%pre

%post
/sbin/ldconfig
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,sacct}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,sacctmgr}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,salloc}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,sattach}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,sbatch}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,sbcast}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,scancel}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,scontrol}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,scrontab}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,sdiag}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,sinfo}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,slurmrestd}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,sprio}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,squeue}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,sreport}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,srun}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,sshare}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,sstat}
ln -sf %{_bashcompdir}/bash-completion/completions/{slurm_completion.sh,strigger}

%preun

%postun
/sbin/ldconfig
if [ $1 -eq 0 ]; then
	rm -f %{_bashcompdir}/bash-completion/completions/sacct
	rm -f %{_bashcompdir}/bash-completion/completions/sacctmgr
	rm -f %{_bashcompdir}/bash-completion/completions/salloc
	rm -f %{_bashcompdir}/bash-completion/completions/sattach
	rm -f %{_bashcompdir}/bash-completion/completions/sbatch
	rm -f %{_bashcompdir}/bash-completion/completions/sbcast
	rm -f %{_bashcompdir}/bash-completion/completions/scancel
	rm -f %{_bashcompdir}/bash-completion/completions/scontrol
	rm -f %{_bashcompdir}/bash-completion/completions/scrontab
	rm -f %{_bashcompdir}/bash-completion/completions/sdiag
	rm -f %{_bashcompdir}/bash-completion/completions/sinfo
	rm -f %{_bashcompdir}/bash-completion/completions/slurmrestd
	rm -f %{_bashcompdir}/bash-completion/completions/sprio
	rm -f %{_bashcompdir}/bash-completion/completions/squeue
	rm -f %{_bashcompdir}/bash-completion/completions/sreport
	rm -f %{_bashcompdir}/bash-completion/completions/srun
	rm -f %{_bashcompdir}/bash-completion/completions/sshare
	rm -f %{_bashcompdir}/bash-completion/completions/sstat
	rm -f %{_bashcompdir}/bash-completion/completions/strigger
fi

%post sackd
%systemd_post sackd.service
%preun sackd
%systemd_preun sackd.service
%postun sackd
%systemd_postun_with_restart sackd.service

%post slurmctld
%systemd_post slurmctld.service
%preun slurmctld
%systemd_preun slurmctld.service
%postun slurmctld
%systemd_postun_with_restart slurmctld.service

%post slurmd
%systemd_post slurmd.service
%preun slurmd
%systemd_preun slurmd.service
%postun slurmd
%systemd_postun_with_restart slurmd.service

%post slurmdbd
%systemd_post slurmdbd.service
%preun slurmdbd
%systemd_preun slurmdbd.service
%postun slurmdbd
%systemd_postun_with_restart slurmdbd.service

%if %{with slurmrestd}
%post slurmrestd
%systemd_post slurmrestd.service
%preun slurmrestd
%systemd_preun slurmrestd.service
%postun slurmrestd
%systemd_postun_with_restart slurmrestd.service
%endif

%if %{defined patch}
%changelog
* %(date "+%a %b %d %Y") %{?packager} - %{version}-%{release}
- Includes patch: %{patch}
%endif
