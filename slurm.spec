Name:		slurm
Version:	17.11.0
Release:	1%{?dist}
Summary:	Slurm Workload Manager

Group:		System Environment/Base
License:	GPLv2+
URL:		https://slurm.schedmd.com/
Source:		%{name}-%{version}.tar.bz2

%description
Slurm is an open source, fault-tolerant, and highly scalable
cluster management and job scheduling system for Linux clusters.
Components include machine status, partition management,
job management, scheduling and accounting modules

# Note that this package is not relocatable

#
# build options      .rpmmacros options      change to default action
# ===============    ====================    ========================
# --enable-multiple-slurmd %_with_multiple_slurmd 1  build with the multiple slurmd
#                                               option.  Typically used to simulate a
#                                               larger system than one has access to.
# --enable-salloc-background %_with_salloc_background 1  on a cray system alloc salloc
#                                               to execute as a background process.
# --prefix           %_prefix             path  install path for commands, libraries, etc.
# --with blcr        %_with_blcr          1     require blcr support
# --with cray        %_with_cray          1     build for a Native-Slurm Cray system
# --with cray_network %_with_cray_network 1     build for a non-Cray system with a Cray network
# --without debug    %_without_debug      1     don't compile with debugging symbols
# --with lua         %_with_lua           1     build Slurm lua bindings
# --with mysql       %_with_mysql         1     require mysql/mariadb support
# --without netloc   %_without_netloc     path  require netloc support
# --with openssl     %_with_openssl       1     require openssl RPM to be installed
# --without pam      %_without_pam        1     don't require pam-devel RPM to be installed
# --without readline %_without_readline   1     don't require readline-devel RPM to be installed
#
#  Allow defining --with and --without build options or %_with and %without in .rpmmacros
#    slurm_with    builds option by default unless --without is specified
#    slurm_without builds option iff --with specified
#
%define slurm_with_opt() %{expand:%%{!?_without_%{1}:%%global slurm_with_%{1} 1}}
%define slurm_without_opt() %{expand:%%{?_with_%{1}:%%global slurm_with_%{1} 1}}
#
#  with helper macro to test for slurm_with_*
#
%define slurm_with() %{expand:%%{?slurm_with_%{1}:1}%%{!?slurm_with_%{1}:0}}

#  Options that are off by default (enable with --with <opt>)
%slurm_without_opt cray
%slurm_without_opt cray_network
%slurm_without_opt salloc_background
%slurm_without_opt multiple_slurmd

# These options are only here to force there to be these on the build.
# If they are not set they will still be compiled if the packages exist.
%slurm_without_opt mysql
%slurm_without_opt blcr
%slurm_without_opt openssl

# Build with OpenSSL by default on all platforms (disable using --without openssl)
%slurm_with_opt openssl

# Use readline by default on all systems
%slurm_with_opt readline

# Use debug by default on all systems
%slurm_with_opt debug

# Build with PAM by default on linux
%ifos linux
%slurm_with_opt pam
%endif

%slurm_without_opt partial-attach

Requires: slurm-plugins
Requires: munge
BuildRequires: munge-devel munge-libs
BuildRequires: python

%if %{slurm_with readline}
BuildRequires: readline-devel
%endif

%if %{slurm_with openssl}
BuildRequires: openssl-devel >= 0.9.6 openssl >= 0.9.6
%endif

%define use_mysql_devel %(perl -e '`rpm -q mariadb-devel`; print $?;')

%if %{slurm_with mysql}
%if %{use_mysql_devel}
BuildRequires: mysql-devel >= 5.0.0
%else
BuildRequires: mariadb-devel >= 5.0.0
%endif
%endif

%if %{slurm_with cray}
BuildRequires: cray-libalpscomm_cn-devel
BuildRequires: cray-libalpscomm_sn-devel
BuildRequires: libnuma-devel
BuildRequires: libhwloc-devel
BuildRequires: cray-libjob-devel
BuildRequires: gtk2-devel
BuildRequires: glib2-devel
BuildRequires: pkg-config
%endif

%if %{slurm_with cray_network}
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
#
# Note: The default value should be 0 for legacy compatibility.
# This was added due to a bug in Suse Linux. For a good reference, see
# http://slforums.typo3-factory.net/index.php?showtopic=11378
%define _unpackaged_files_terminate_build      0

# First we remove $prefix/local and then just prefix to make
# sure we get the correct installdir
%define _perlarch %(perl -e 'use Config; $T=$Config{installsitearch}; $P=$Config{installprefix}; $P1="$P/local"; $T =~ s/$P1//; $T =~ s/$P//; print $T;')

%define _perlman3 %(perl -e 'use Config; $T=$Config{installsiteman3dir}; $P=$Config{siteprefix}; $P1="$P/local"; $T =~ s/$P1//; $T =~ s/$P//; print $T;')

%define _perlarchlib %(perl -e 'use Config; $T=$Config{installarchlib}; $P=$Config{installprefix}; $P1="$P/local"; $T =~ s/$P1//; $T =~ s/$P//; print $T;')

%define _perldir %{_prefix}%{_perlarch}
%define _perlman3dir %{_prefix}%{_perlman3}
%define _perlarchlibdir %{_prefix}%{_perlarchlib}
%define _php_extdir %(php-config --extension-dir 2>/dev/null || echo %{_libdir}/php5)

%package perlapi
Summary: Perl API to Slurm
Group: Development/System
Requires: slurm
%description perlapi
Perl API package for Slurm.  This package includes the perl API to provide a
helpful interface to Slurm through Perl

%package devel
Summary: Development package for Slurm
Group: Development/System
Requires: slurm
%description devel
Development package for Slurm.  This package includes the header files
and static libraries for the Slurm API

%package slurmdbd
Summary: Slurm database daemon
Group: System Environment/Base
Requires: slurm-plugins
%description slurmdbd
Slurm database daemon. Used to accept and process database RPCs and upload
database changes to slurmctld daemons on each cluster

%package plugins
Summary: Slurm plugins (loadable shared objects)
Group: System Environment/Base
%description plugins
Slurm plugins (loadable shared objects) supporting a wide variety of
architectures and behaviors. These basically provide the building blocks
with which Slurm can be configured. Note that some system specific plugins
are in other packages

%package torque
Summary: Torque/PBS wrappers for transitition from Torque/PBS to Slurm
Group: Development/System
Requires: slurm-perlapi
%description torque
Torque wrapper scripts used for helping migrate from Torque/PBS to Slurm

%package openlava
Summary: openlava/LSF wrappers for transitition from OpenLava/LSF to Slurm
Group: Development/System
Requires: slurm-perlapi
%description openlava
OpenLava wrapper scripts used for helping migrate from OpenLava/LSF to Slurm

%package contribs
Summary: Perl tool to print Slurm job state information
Group: Development/System
Requires: slurm
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

%if %{slurm_with pam}
%package pam_slurm
Summary: PAM module for restricting access to compute nodes via Slurm
Group: System Environment/Base
Requires: slurm
BuildRequires: pam-devel
Obsoletes: pam_slurm
%description pam_slurm
This module restricts access to compute nodes in a cluster where Slurm is in
use.  Access is granted to root, any user with an Slurm-launched job currently
running on the node, or any user who has allocated resources on the node
according to the Slurm
%endif

#############################################################################

%prep
%setup -n %{name}-%{version}

%build
%configure \
	%{!?slurm_with_debug:--disable-debug} \
	%{?slurm_with_partial_attach:--enable-partial-attach} \
	%{?with_pam_dir:--with-pam_dir=%{?with_pam_dir}} \
	%{?with_proctrack:--with-proctrack=%{?with_proctrack}}\
	%{?with_cpusetdir:--with-cpusetdir=%{?with_cpusetdir}} \
	%{?with_mysql_config:--with-mysql_config=%{?with_mysql_config}} \
	%{?with_ssl:--with-ssl=%{?with_ssl}} \
	%{?with_netloc:--with-netloc=%{?with_netloc}}\
	%{?with_blcr:--with-blcr=%{?with_blcr}}\
	%{?slurm_with_cray:--enable-native-cray}\
	%{?slurm_with_cray_network:--enable-cray-network}\
	%{?slurm_with_salloc_background:--enable-salloc-background} \
	%{!?slurm_with_readline:--without-readline} \
	%{?slurm_with_multiple_slurmd:--enable-multiple-slurmd} \
	%{?slurm_with_pmix:--with-pmix=%{?slurm_with_pmix}} \
	%{?with_freeipmi:--with-freeipmi=%{?with_freeipmi}}\
        %{?slurm_without_shared_libslurm:--without-shared-libslurm}\
        %{?with_cflags} \

make %{?_smp_mflags}

%install


# Strip out some dependencies

cat > find-requires.sh <<'EOF'
exec %{__find_requires} "$@" | egrep -v '^libpmix.so|libevent'
EOF
chmod +x find-requires.sh
%global _use_internal_dependency_generator 0
%global __find_requires %{_builddir}/%{buildsubdir}/find-requires.sh

rm -rf {%buildroot}
make install DESTDIR=%{buildroot}
make install-contrib DESTDIR=%{buildroot}

install -D -m644 etc/slurmctld.service %{buildroot}/usr/lib/systemd/system/slurmctld.service
install -D -m644 etc/slurmd.service    %{buildroot}/usr/lib/systemd/system/slurmd.service
install -D -m644 etc/slurmdbd.service  %{buildroot}/usr/lib/systemd/system/slurmdbd.service

# Do not package Slurm's version of libpmi on Cray systems.
# Cray's version of libpmi should be used.
%if %{slurm_with cray}
   rm -f %{buildroot}/%{_libdir}/libpmi*
   %if %{slurm_with cray}
      install -D -m644 contribs/cray/plugstack.conf.template ${RPM_BUILD_ROOT}%{_sysconfdir}/plugstack.conf.template
      install -D -m644 contribs/cray/slurm.conf.template ${RPM_BUILD_ROOT}%{_sysconfdir}/slurm.conf.template
   %endif
   install -D -m644 contribs/cray/opt_modulefiles_slurm $RPM_BUILD_ROOT/opt/modulefiles/slurm/%{version}-%{release}
   echo -e '#%Module\nset ModulesVersion "%{version}-%{release}"' > $RPM_BUILD_ROOT/opt/modulefiles/slurm/.version
%else
   rm -f contribs/cray/opt_modulefiles_slurm
   rm -f $RPM_BUILD_ROOT/%{_sysconfdir}/plugstack.conf.template
   rm -f $RPM_BUILD_ROOT/%{_sysconfdir}/slurm.conf.template
   rm -f $RPM_BUILD_ROOT/%{_sbindir}/capmc_suspend
   rm -f $RPM_BUILD_ROOT/%{_sbindir}/capmc_resume
   rm -f $RPM_BUILD_ROOT/%{_sbindir}/slurmconfgen.py
%endif

install -D -m644 etc/cgroup.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/cgroup.conf.example
install -D -m644 etc/cgroup_allowed_devices_file.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/cgroup_allowed_devices_file.conf.example
install -D -m644 etc/layouts.d.power.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/layouts.d/power.conf.example
install -D -m644 etc/layouts.d.power_cpufreq.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/layouts.d/power_cpufreq.conf.example
install -D -m644 etc/layouts.d.unit.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/layouts.d/unit.conf.example
install -D -m644 etc/slurm.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/slurm.conf.example
install -D -m755 etc/slurm.epilog.clean ${RPM_BUILD_ROOT}%{_sysconfdir}/slurm.epilog.clean
install -D -m644 etc/slurmdbd.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/slurmdbd.conf.example
install -D -m755 contribs/sjstat ${RPM_BUILD_ROOT}%{_bindir}/sjstat

# Delete unpackaged files:
rm -f $RPM_BUILD_ROOT/%{_libdir}/*.a
rm -f $RPM_BUILD_ROOT/%{_libdir}/*.la
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/*.a
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/*.la
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/job_submit_defaults.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/job_submit_logging.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/job_submit_partition.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/security/*.a
rm -f $RPM_BUILD_ROOT/%{_libdir}/security/*.la
%if %{?with_pam_dir}0
rm -f $RPM_BUILD_ROOT/%{with_pam_dir}/pam_slurm.a
rm -f $RPM_BUILD_ROOT/%{with_pam_dir}/pam_slurm.la
rm -f $RPM_BUILD_ROOT/%{with_pam_dir}/pam_slurm_adopt.a
rm -f $RPM_BUILD_ROOT/%{with_pam_dir}/pam_slurm_adopt.la
%endif
rm -f $RPM_BUILD_ROOT/lib/security/pam_slurm.a
rm -f $RPM_BUILD_ROOT/lib/security/pam_slurm.la
rm -f $RPM_BUILD_ROOT/lib32/security/pam_slurm.a
rm -f $RPM_BUILD_ROOT/lib32/security/pam_slurm.la
rm -f $RPM_BUILD_ROOT/lib64/security/pam_slurm.a
rm -f $RPM_BUILD_ROOT/lib64/security/pam_slurm.la
rm -f $RPM_BUILD_ROOT/lib/security/pam_slurm_adopt.a
rm -f $RPM_BUILD_ROOT/lib/security/pam_slurm_adopt.la
rm -f $RPM_BUILD_ROOT/lib32/security/pam_slurm_adopt.a
rm -f $RPM_BUILD_ROOT/lib32/security/pam_slurm_adopt.la
rm -f $RPM_BUILD_ROOT/lib64/security/pam_slurm_adopt.a
rm -f $RPM_BUILD_ROOT/lib64/security/pam_slurm_adopt.la
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/auth_none.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/launch_poe.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/libpermapi.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/libsched_if.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/libsched_if64.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/proctrack_sgi_job.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/runjob_plugin.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/select_bluegene.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/switch_nrt.so
rm -f $RPM_BUILD_ROOT/%{_mandir}/man5/bluegene*
rm -f $RPM_BUILD_ROOT/%{_sbindir}/sfree
rm -f $RPM_BUILD_ROOT/%{_sbindir}/slurm_epilog
rm -f $RPM_BUILD_ROOT/%{_sbindir}/slurm_prolog
rm -f $RPM_BUILD_ROOT/%{_sysconfdir}/init.d/slurm
rm -f $RPM_BUILD_ROOT/%{_sysconfdir}/init.d/slurmdbd
rm -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurm/.packlist
rm -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurm/Slurm.bs
rm -f $RPM_BUILD_ROOT/%{_perlarchlibdir}/perllocal.pod
rm -f $RPM_BUILD_ROOT/%{_perldir}/perllocal.pod
rm -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurmdb/.packlist
rm -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurmdb/Slurmdb.bs

# Build man pages that are generated directly by the tools
rm -f $RPM_BUILD_ROOT/%{_mandir}/man1/sjobexitmod.1
${RPM_BUILD_ROOT}%{_bindir}/sjobexitmod --roff > $RPM_BUILD_ROOT/%{_mandir}/man1/sjobexitmod.1
rm -f $RPM_BUILD_ROOT/%{_mandir}/man1/sjstat.1
${RPM_BUILD_ROOT}%{_bindir}/sjstat --roff > $RPM_BUILD_ROOT/%{_mandir}/man1/sjstat.1

# Build conditional file list for main package
LIST=./slurm.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libexecdir}/slurm/cr_checkpoint.sh   &&
  echo %{_libexecdir}/slurm/cr_checkpoint.sh	        >> $LIST
test -f $RPM_BUILD_ROOT/%{_libexecdir}/slurm/cr_restart.sh      &&
  echo %{_libexecdir}/slurm/cr_restart.sh	        >> $LIST
test -f $RPM_BUILD_ROOT/%{_sbindir}/capmc_suspend		&&
  echo %{_sbindir}/capmc_suspend			>> $LIST
test -f $RPM_BUILD_ROOT/%{_sbindir}/capmc_resume		&&
  echo %{_sbindir}/capmc_resume				>> $LIST
test -f $RPM_BUILD_ROOT/usr/lib/systemd/system/slurmctld.service	&&
  echo /usr/lib/systemd/system/slurmctld.service		>> $LIST
test -f $RPM_BUILD_ROOT/usr/lib/systemd/system/slurmd.service	&&
  echo /usr/lib/systemd/system/slurmd.service		>> $LIST
test -f $RPM_BUILD_ROOT/%{_bindir}/netloc_to_topology		&&
  echo %{_bindir}/netloc_to_topology			>> $LIST

test -f $RPM_BUILD_ROOT/opt/modulefiles/slurm/%{version}-%{release} &&
  echo /opt/modulefiles/slurm/%{version}-%{release} >> $LIST
test -f $RPM_BUILD_ROOT/opt/modulefiles/slurm/.version &&
  echo /opt/modulefiles/slurm/.version >> $LIST

# Make ld.so.conf.d file
mkdir -p $RPM_BUILD_ROOT/etc/ld.so.conf.d
echo '%{_libdir}
%{_libdir}/slurm' > $RPM_BUILD_ROOT/etc/ld.so.conf.d/slurm.conf
chmod 644 $RPM_BUILD_ROOT/etc/ld.so.conf.d/slurm.conf

# Make pkg-config file
mkdir -p $RPM_BUILD_ROOT/%{_libdir}/pkgconfig
cat >$RPM_BUILD_ROOT/%{_libdir}/pkgconfig/slurm.pc <<EOF
includedir=%{_prefix}/include
libdir=%{_libdir}

Cflags: -I\${includedir}
Libs: -L\${libdir} -lslurm
Description: Slurm API
Name: %{name}
Version: %{version}
EOF

LIST=./slurmdbd.files
touch $LIST
test -f $RPM_BUILD_ROOT/usr/lib/systemd/system/slurmdbd.service	&&
  echo /usr/lib/systemd/system/slurmdbd.service		>> $LIST

LIST=./pam.files
touch $LIST
%if %{?with_pam_dir}0
    test -f $RPM_BUILD_ROOT/%{with_pam_dir}/pam_slurm.so	&&
	echo %{with_pam_dir}/pam_slurm.so	>>$LIST
    test -f $RPM_BUILD_ROOT/%{with_pam_dir}/pam_slurm_adopt.so	&&
	echo %{with_pam_dir}/pam_slurm_adopt.so	>>$LIST
%else
    test -f $RPM_BUILD_ROOT/lib/security/pam_slurm.so	&&
	echo /lib/security/pam_slurm.so		>>$LIST
    test -f $RPM_BUILD_ROOT/lib32/security/pam_slurm.so	&&
	echo /lib32/security/pam_slurm.so	>>$LIST
    test -f $RPM_BUILD_ROOT/lib64/security/pam_slurm.so	&&
	echo /lib64/security/pam_slurm.so	>>$LIST
    test -f $RPM_BUILD_ROOT/lib/security/pam_slurm_adopt.so		&&
	echo /lib/security/pam_slurm_adopt.so		>>$LIST
    test -f $RPM_BUILD_ROOT/lib32/security/pam_slurm_adopt.so		&&
	echo /lib32/security/pam_slurm_adopt.so		>>$LIST
    test -f $RPM_BUILD_ROOT/lib64/security/pam_slurm_adopt.so		&&
	echo /lib64/security/pam_slurm_adopt.so		>>$LIST
%endif
#############################################################################

%clean
rm -rf $RPM_BUILD_ROOT
#############################################################################

%files -f slurm.files
%defattr(-,root,root,0755)
%{_datadir}/doc
%{_bindir}/s*
%exclude %{_bindir}/seff
%exclude %{_bindir}/sjobexitmod
%exclude %{_bindir}/sjstat
%exclude %{_bindir}/smail
%{_sbindir}/slurmctld
%{_sbindir}/slurmd
%{_sbindir}/slurmstepd
%{_libdir}/*.so*
%{_libdir}/slurm/src/*
%{_mandir}/man1/*
%{_mandir}/man5/acct_gather.*
%{_mandir}/man5/burst_buffer.*
%{_mandir}/man5/cgroup.*
%{_mandir}/man5/cray.*
%{_mandir}/man5/ext_sensors.*
%{_mandir}/man5/gres.*
%{_mandir}/man5/knl.*
%{_mandir}/man5/nonstop.*
%{_mandir}/man5/slurm.*
%{_mandir}/man5/topology.*
%{_mandir}/man8/slurmctld.*
%{_mandir}/man8/slurmd.*
%{_mandir}/man8/slurmstepd*
%{_mandir}/man8/spank*
%dir %{_sysconfdir}
%dir %{_libdir}/slurm/src
%dir /etc/ld.so.conf.d
/etc/ld.so.conf.d/slurm.conf
%if %{slurm_with cray}
%dir /opt/modulefiles/slurm
%endif
%if %{slurm_with cray}
%config %{_sysconfdir}/plugstack.conf.template
%config %{_sysconfdir}/slurm.conf.template
%{_sbindir}/slurmconfgen.py
%endif
%config %{_sysconfdir}/cgroup.conf.example
%config %{_sysconfdir}/cgroup_allowed_devices_file.conf.example
%config %{_sysconfdir}/layouts.d/power.conf.example
%config %{_sysconfdir}/layouts.d/power_cpufreq.conf.example
%config %{_sysconfdir}/layouts.d/unit.conf.example
%config %{_sysconfdir}/slurm.conf.example
%config %{_sysconfdir}/slurm.epilog.clean
%exclude %{_mandir}/man1/sjobexit*
%exclude %{_mandir}/man1/sjstat*
#############################################################################

%files devel
%defattr(-,root,root)
%dir %attr(0755,root,root)
%dir %{_prefix}/include/slurm
%{_prefix}/include/slurm/*
%{_mandir}/man3/slurm_*
%dir %{_libdir}/pkgconfig
%{_libdir}/pkgconfig/slurm.pc
#%{_mandir}/man3/slurmdb_*
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

%files -f slurmdbd.files slurmdbd
%defattr(-,root,root)
%{_sbindir}/slurmdbd
%{_libdir}/slurm/accounting_storage_mysql.so
%{_mandir}/man5/slurmdbd.*
%{_mandir}/man8/slurmdbd.*
%config %{_sysconfdir}/slurmdbd.conf.example
#############################################################################

%files plugins
%defattr(-,root,root)
%dir %{_libdir}/slurm
%{_libdir}/slurm/*.so
%exclude %{_libdir}/slurm/accounting_storage_mysql.so
%exclude %{_libdir}/slurm/job_submit_pbs.so
%exclude %{_libdir}/slurm/spank_pbs.so
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

%if %{slurm_with pam}
%files -f pam.files pam_slurm
%defattr(-,root,root)
%endif
#############################################################################

%pre

%post

%preun

%preun slurmdbd

%postun

%postun slurmdbd
