Name:		slurm
Version:	17.11.7
%global rel	1
Release:	%{rel}%{?dist}
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

# build options		.rpmmacros options	change to default action
# ====================  ====================	========================
# --prefix		%_prefix path		install path for commands, libraries, etc.
# --with cray		%_with_cray 1		build for a Native-Slurm Cray system
# --with cray_network	%_with_cray_network 1	build for a non-Cray system with a Cray network
# --without debug	%_without_debug 1	don't compile with debugging symbols
# --with hdf5		%_with_hdf5 path	require hdf5 support
# --with hwloc		%_with_hwloc 1		require hwloc support
# --with lua		%_with_lua path		build Slurm lua bindings
# --with mysql		%_with_mysql 1		require mysql/mariadb support
# --with numa		%_with_numa 1		require NUMA support
# --with openssl	%_with_openssl 1	require openssl RPM to be installed
#						ensures auth/openssl and crypto/openssl are built
# --without pam		%_without_pam 1		don't require pam-devel RPM to be installed

#  Options that are off by default (enable with --with <opt>)
%bcond_with cray
%bcond_with cray_network
%bcond_with multiple_slurmd

# These options are only here to force there to be these on the build.
# If they are not set they will still be compiled if the packages exist.
%bcond_with hwloc
%bcond_with mysql
%bcond_with hdf5
%bcond_with lua
%bcond_with numa

# Build with OpenSSL by default on all platforms (disable using --without openssl)
%bcond_without openssl

# Use debug by default on all systems
%bcond_without debug

# Build with PAM by default on linux
%bcond_without pam

Requires: munge

%{?systemd_requires}
BuildRequires: systemd
BuildRequires: munge-devel munge-libs
BuildRequires: python
BuildRequires: readline-devel
Obsoletes: slurm-lua slurm-munge slurm-plugins

# fake systemd support when building rpms on other platforms
%{!?_unitdir: %global _unitdir /lib/systemd/systemd}

%if %{with openssl}
BuildRequires: openssl-devel >= 0.9.6 openssl >= 0.9.6
%endif

%define use_mysql_devel %(perl -e '`rpm -q mariadb-devel`; print $?;')

%if %{with mysql}
%if %{use_mysql_devel}
BuildRequires: mysql-devel >= 5.0.0
%else
BuildRequires: mariadb-devel >= 5.0.0
%endif
%endif

%if %{with cray}
BuildRequires: cray-libalpscomm_cn-devel
BuildRequires: cray-libalpscomm_sn-devel
BuildRequires: libnuma-devel
BuildRequires: libhwloc-devel
BuildRequires: cray-libjob-devel
BuildRequires: gtk2-devel
BuildRequires: glib2-devel
BuildRequires: pkg-config
%endif

%if %{with cray_network}
%if %{use_mysql_devel}
BuildRequires: mysql-devel
%else
BuildRequires: mariadb-devel
%endif
BuildRequires: cray-libalpscomm_cn-devel
BuildRequires: cray-libalpscomm_sn-devel
BuildRequires: hwloc-devel
BuildRequires: gtk2-devel
BuildRequires: glib2-devel
BuildRequires: pkgconfig
%endif

BuildRequires: perl(ExtUtils::MakeMaker)

%if %{with lua}
BuildRequires: pkgconfig(lua) >= 5.1.0
%endif

%if %{with hwloc}
BuildRequires: hwloc-devel
%endif

%if %{with numa}
%if %{defined suse_version}
BuildRequires: libnuma-devel
%else
BuildRequires: numactl-devel
%endif
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

%package slurmctld
Summary: Slurm controller daemon
Group: System Environment/Base
Requires: %{name}%{?_isa} = %{version}-%{release}
%description slurmctld
Slurm controller daemon. Used to manage the job queue, schedule jobs,
and dispatch RPC messages to the slurmd processon the compute nodes
to launch jobs.

%package slurmd
Summary: Slurm compute node daemon
Group: System Environment/Base
Requires: %{name}%{?_isa} = %{version}-%{release}
%description slurmd
Slurm compute node daemon. Used to launch jobs on compute nodes

%package slurmdbd
Summary: Slurm database daemon
Group: System Environment/Base
Requires: %{name}%{?_isa} = %{version}-%{release}
Obsoletes: slurm-sql
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
Obsoletes: slurm-sjobexit slurm-sjstat slurm-seff
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
Obsoletes: pam_slurm
%description pam_slurm
This module restricts access to compute nodes in a cluster where Slurm is in
use.  Access is granted to root, any user with an Slurm-launched job currently
running on the node, or any user who has allocated resources on the node
according to the Slurm
%endif

%if %{with cray}
%package slurmsmwd
Summary: support daemons and software for the Cray SMW
Group: System Environment/Base
Requires: %{name}%{?_isa} = %{version}-%{release}
Obsoletes: craysmw
%description slurmsmwd
support daeamons and software for the Cray SMW.  Includes slurmsmwd which
notifies slurm about failed nodes.
%endif

#############################################################################

%prep
# when the rel number is one, the tarball filename does not include it
%setup -n %{slurm_source_dir}

%build
%configure \
	%{?_without_debug:--disable-debug} \
	%{?_with_pam_dir} \
	%{?_with_cpusetdir} \
	%{?_with_mysql_config} \
	%{?_with_ssl} \
	%{?_without_cray:--disable-native-cray}\
	%{?_with_cray_network:--enable-cray-network}\
	%{?_with_multiple_slurmd:--enable-multiple-slurmd} \
	%{?_with_pmix} \
	%{?_with_freeipmi} \
	%{?_with_hdf5} \
	%{?_with_shared_libslurm} \
	%{?_with_cflags}

make %{?_smp_mflags}

%install

# Strip out some dependencies

cat > find-requires.sh <<'EOF'
exec %{__find_requires} "$@" | egrep -v '^libpmix.so|libevent'
EOF
chmod +x find-requires.sh
%global _use_internal_dependency_generator 0
%global __find_requires %{_builddir}/%{buildsubdir}/find-requires.sh

rm -rf %{buildroot}
make install DESTDIR=%{buildroot}
make install-contrib DESTDIR=%{buildroot}

install -D -m644 etc/slurmctld.service %{buildroot}/%{_unitdir}/slurmctld.service
install -D -m644 etc/slurmd.service    %{buildroot}/%{_unitdir}/slurmd.service
install -D -m644 etc/slurmdbd.service  %{buildroot}/%{_unitdir}/slurmdbd.service

# Do not package Slurm's version of libpmi on Cray systems in the usual location.
# Cray's version of libpmi should be used. Move it elsewhere if the site still
# wants to use it with other MPI stacks.
%if %{with cray}
   mkdir %{buildroot}/%{_libdir}/slurmpmi
   mv %{buildroot}/%{_libdir}/libpmi* %{buildroot}/%{_libdir}/slurmpmi
   install -D -m644 contribs/cray/plugstack.conf.template %{buildroot}/%{_sysconfdir}/plugstack.conf.template
   install -D -m644 contribs/cray/slurm.conf.template %{buildroot}/%{_sysconfdir}/slurm.conf.template
   mkdir -p %{buildroot}/opt/modulefiles/slurm
   test -f contribs/cray/opt_modulefiles_slurm &&
      install -D -m644 contribs/cray/opt_modulefiles_slurm %{buildroot}/opt/modulefiles/slurm/%{version}-%{rel}
   install -D -m644 contribs/cray/slurmsmwd/slurmsmwd.service %{buildroot}/%{_unitdir}/slurmsmwd.service
   echo -e '#%Module\nset ModulesVersion "%{version}-%{rel}"' > %{buildroot}/opt/modulefiles/slurm/.version
%else
   rm -f contribs/cray/opt_modulefiles_slurm
   rm -f contribs/cray/slurmsmwd/slurmsmwd.service
   rm -f %{buildroot}/%{_sysconfdir}/plugstack.conf.template
   rm -f %{buildroot}/%{_sysconfdir}/slurm.conf.template
   rm -f %{buildroot}/%{_sbindir}/capmc_suspend
   rm -f %{buildroot}/%{_sbindir}/capmc_resume
   rm -f %{buildroot}/%{_sbindir}/slurmconfgen.py
   rm -f %{buildroot}/%{_sbindir}/slurmsmwd
%endif

install -D -m644 etc/cgroup.conf.example %{buildroot}/%{_sysconfdir}/cgroup.conf.example
install -D -m644 etc/cgroup_allowed_devices_file.conf.example %{buildroot}/%{_sysconfdir}/cgroup_allowed_devices_file.conf.example
install -D -m644 etc/layouts.d.power.conf.example %{buildroot}/%{_sysconfdir}/layouts.d/power.conf.example
install -D -m644 etc/layouts.d.power_cpufreq.conf.example %{buildroot}/%{_sysconfdir}/layouts.d/power_cpufreq.conf.example
install -D -m644 etc/layouts.d.unit.conf.example %{buildroot}/%{_sysconfdir}/layouts.d/unit.conf.example
install -D -m644 etc/slurm.conf.example %{buildroot}/%{_sysconfdir}/slurm.conf.example
install -D -m755 etc/slurm.epilog.clean %{buildroot}/%{_sysconfdir}/slurm.epilog.clean
install -D -m644 etc/slurmdbd.conf.example %{buildroot}/%{_sysconfdir}/slurmdbd.conf.example
install -D -m755 contribs/sjstat %{buildroot}/%{_bindir}/sjstat

# Delete unpackaged files:
find %{buildroot} -name '*.a' -exec rm {} \;
find %{buildroot} -name '*.la' -exec rm {} \;
rm -f %{buildroot}/%{_libdir}/slurm/job_submit_defaults.so
rm -f %{buildroot}/%{_libdir}/slurm/job_submit_logging.so
rm -f %{buildroot}/%{_libdir}/slurm/job_submit_partition.so
rm -f %{buildroot}/%{_libdir}/slurm/auth_none.so
rm -f %{buildroot}/%{_libdir}/slurm/launch_poe.so
rm -f %{buildroot}/%{_libdir}/slurm/libpermapi.so
rm -f %{buildroot}/%{_libdir}/slurm/libsched_if.so
rm -f %{buildroot}/%{_libdir}/slurm/libsched_if64.so
rm -f %{buildroot}/%{_libdir}/slurm/proctrack_sgi_job.so
rm -f %{buildroot}/%{_libdir}/slurm/runjob_plugin.so
rm -f %{buildroot}/%{_libdir}/slurm/select_bluegene.so
rm -f %{buildroot}/%{_libdir}/slurm/switch_nrt.so
rm -f %{buildroot}/%{_mandir}/man5/bluegene*
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

# Build man pages that are generated directly by the tools
rm -f %{buildroot}/%{_mandir}/man1/sjobexitmod.1
%{buildroot}/%{_bindir}/sjobexitmod --roff > %{buildroot}/%{_mandir}/man1/sjobexitmod.1
rm -f %{buildroot}/%{_mandir}/man1/sjstat.1
%{buildroot}/%{_bindir}/sjstat --roff > %{buildroot}/%{_mandir}/man1/sjstat.1

# Build conditional file list for main package
LIST=./slurm.files
touch $LIST
test -f %{buildroot}/%{_libexecdir}/slurm/cr_checkpoint.sh   &&
  echo %{_libexecdir}/slurm/cr_checkpoint.sh	        >> $LIST
test -f %{buildroot}/%{_libexecdir}/slurm/cr_restart.sh      &&
  echo %{_libexecdir}/slurm/cr_restart.sh	        >> $LIST
test -f %{buildroot}/%{_sbindir}/capmc_suspend		&&
  echo %{_sbindir}/capmc_suspend			>> $LIST
test -f %{buildroot}/%{_sbindir}/capmc_resume		&&
  echo %{_sbindir}/capmc_resume				>> $LIST
test -f %{buildroot}/%{_bindir}/netloc_to_topology		&&
  echo %{_bindir}/netloc_to_topology			>> $LIST

test -f %{buildroot}/opt/modulefiles/slurm/%{version}-%{rel} &&
  echo /opt/modulefiles/slurm/%{version}-%{rel} >> $LIST
test -f %{buildroot}/opt/modulefiles/slurm/.version &&
  echo /opt/modulefiles/slurm/.version >> $LIST


LIST=./example.configs
touch $LIST
%if %{with cray}
   test -f %{buildroot}/%{_sbindir}/slurmconfgen.py	&&
	echo %{_sbindir}/slurmconfgen.py		>>$LIST
%endif

# Make pkg-config file
mkdir -p %{buildroot}/%{_libdir}/pkgconfig
cat >%{buildroot}/%{_libdir}/pkgconfig/slurm.pc <<EOF
includedir=%{_prefix}/include
libdir=%{_libdir}

Cflags: -I\${includedir}
Libs: -L\${libdir} -lslurm
Description: Slurm API
Name: %{name}
Version: %{version}
EOF

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

%files -f slurm.files
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
%if %{with cray}
%dir /opt/modulefiles/slurm
%endif
#############################################################################

%files -f example.configs example-configs
%defattr(-,root,root,0755)
%dir %{_sysconfdir}
%if %{with cray}
%config %{_sysconfdir}/plugstack.conf.template
%config %{_sysconfdir}/slurm.conf.template
%endif
%config %{_sysconfdir}/cgroup.conf.example
%config %{_sysconfdir}/cgroup_allowed_devices_file.conf.example
%config %{_sysconfdir}/layouts.d/power.conf.example
%config %{_sysconfdir}/layouts.d/power_cpufreq.conf.example
%config %{_sysconfdir}/layouts.d/unit.conf.example
%config %{_sysconfdir}/slurm.conf.example
%config %{_sysconfdir}/slurm.epilog.clean
%config %{_sysconfdir}/slurmdbd.conf.example
#############################################################################

%files devel
%defattr(-,root,root)
%dir %attr(0755,root,root)
%dir %{_prefix}/include/slurm
%{_prefix}/include/slurm/*
%dir %{_libdir}/pkgconfig
%{_libdir}/pkgconfig/slurm.pc
#############################################################################

%files perlapi
%defattr(-,root,root)
%{_perldir}/Slurm.pm
%{_perldir}/Slurm/Bitstr.pm
%{_perldir}/Slurm/Constant.pm
%{_perldir}/Slurm/Hostlist.pm
%{_perldir}/Slurm/Stepctx.pm
%{_perldir}/auto/Slurm/Slurm.so
%{_perldir}/Slurmdb.pm
%{_perldir}/auto/Slurmdb/Slurmdb.so
%{_perldir}/auto/Slurmdb/autosplit.ix
%{_perlman3dir}/Slurm*

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
%if %{with cray}
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

%if %{with cray}
%files slurmsmwd
%{_sbindir}/slurmsmwd
%{_unitdir}/slurmsmwd.service
%endif
#############################################################################

%pre

%post
/sbin/ldconfig

%preun

%postun
/sbin/ldconfig

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
