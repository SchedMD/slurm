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
# --with auth_none   %_with_auth_none     1     build auth-none RPM
# --with blcr        %_with_blcr          1     require blcr support
# --with bluegene    %_with_bluegene      1     build bluegene RPM
# --with cray        %_with_cray          1     build for a Cray system without ALPS
# --with cray_alps   %_with_cray_alps     1     build for a Cray system with ALPS
# --with cray_network %_with_cray_network 1     build for a non-Cray system with a Cray network
# --without debug    %_without_debug      1     don't compile with debugging symbols
# --with lua         %_with_lua           1     build Slurm lua bindings (proctrack only for now)
# --without munge    %_without_munge      path  don't build auth-munge RPM
# --with mysql       %_with_mysql         1     require mysql/mariadb support
# --without netloc   %_without_netloc     path  require netloc support
# --with openssl     %_with_openssl       1     require openssl RPM to be installed
# --without pam      %_without_pam        1     don't require pam-devel RPM to be installed
# --with percs       %_with_percs         1     build percs RPM
# --without readline %_without_readline   1     don't require readline-devel RPM to be installed
# --with sgijob      %_with_sgijob        1     build proctrack-sgi-job RPM
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
%slurm_without_opt auth_none
%slurm_without_opt bluegene
%slurm_without_opt cray
%slurm_without_opt cray_alps
%slurm_without_opt cray_network
%slurm_without_opt salloc_background
%slurm_without_opt multiple_slurmd

# These options are only here to force there to be these on the build.
# If they are not set they will still be compiled if the packages exist.
%slurm_without_opt mysql
%slurm_without_opt blcr
%slurm_without_opt openssl

# Build with munge by default on all platforms (disable using --without munge)
%slurm_with_opt munge

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

%slurm_without_opt sgijob
%slurm_without_opt lua
%slurm_without_opt partial-attach

%if %{slurm_with cray_alps}
%slurm_with_opt sgijob
%endif

Name:    see META file
Version: see META file
Release: see META file

Summary: Slurm Workload Manager

License: GPL
Group: System Environment/Base
Source: %{name}-%{version}-%{release}.tgz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
URL: https://slurm.schedmd.com/

Requires: slurm-plugins

%ifos linux
BuildRequires: python
%endif

%ifos solaris
Requires:	SUNWgnome-base-libs
BuildRequires:	SUNWgnome-base-libs

Requires:	SUNWopenssl
BuildRequires:	SUNWopenssl

BuildRequires:	SUNWaconf
BuildRequires:	SUNWgnu-automake-110
BuildRequires:	SUNWlibtool
BuildRequires:	SUNWgcc
BuildRequires:	SUNWgnome-common-devel
%endif

# not sure if this is always an actual rpm or not so leaving the requirement out
#%if %{slurm_with blcr}
#BuildRequires: blcr
#%endif

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

%if %{slurm_with cray_alps}
%if %{use_mysql_devel}
BuildRequires: mysql-devel
%else
BuildRequires: mariadb-devel
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

%description
Slurm is an open source, fault-tolerant, and highly
scalable cluster management and job scheduling system for Linux clusters.
Components include machine status, partition management, job management,
scheduling and accounting modules

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

#  Allow override of infodir via _slurm_infodir.
#  (Not currently used for anything)
%{!?_slurm_infodir: %global _slurm_infodir %{_datadir}/info}
%define _infodir %{_slurm_infodir}


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

%if %{slurm_with auth_none}
%package auth-none
Summary: Slurm auth NULL implementation (no authentication)
Group: System Environment/Base
Requires: slurm
%description auth-none
Slurm NULL authentication module
%endif

# This is named munge instead of auth-munge since there are 2 plugins in the
# package.  auth-munge and crypto-munge
%if %{slurm_with munge}
%package munge
Summary: Slurm authentication and crypto implementation using Munge
Group: System Environment/Base
Requires: slurm munge
BuildRequires: munge-devel munge-libs
Obsoletes: slurm-auth-munge
%description munge
Slurm authentication and crypto implementation using Munge. Used to
authenticate user originating an RPC, digitally sign and/or encrypt messages
%endif

%if %{slurm_with bluegene}
%package bluegene
Summary: Slurm interfaces to IBM Blue Gene system
Group: System Environment/Base
Requires: slurm
%description bluegene
Slurm plugin interfaces to IBM Blue Gene system
%endif

%package slurmdbd
Summary: Slurm database daemon
Group: System Environment/Base
Requires: slurm-plugins slurm-sql
%description slurmdbd
Slurm database daemon. Used to accept and process database RPCs and upload
database changes to slurmctld daemons on each cluster

%package sql
Summary: Slurm SQL support
Group: System Environment/Base
%description sql
Slurm SQL support. Contains interfaces to MySQL.

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

%if %{slurm_with percs}
%package percs
Summary: Slurm plugins to run on an IBM PERCS system
Group: System Environment/Base
Requires: slurm nrt
BuildRequires: nrt
%description percs
Slurm plugins to run on an IBM PERCS system, POE interface and NRT switch plugin
%endif

%if %{slurm_with sgijob}
%package proctrack-sgi-job
Summary: Slurm process tracking plugin for SGI job containers
Group: System Environment/Base
Requires: slurm
BuildRequires: job
%description proctrack-sgi-job
Slurm process tracking plugin for SGI job containers
(See http://oss.sgi.com/projects/pagg)
%endif

%if %{slurm_with lua}
%package lua
Summary: Slurm lua bindings
Group: System Environment/Base
Requires: slurm lua
BuildRequires: lua-devel
%description lua
Slurm lua bindings
Includes the Slurm proctrack/lua and job_submit/lua plugin
%endif

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
%setup -n %{name}-%{version}-%{release}

%build
%configure \
	%{!?slurm_with_debug:--disable-debug} \
	%{?slurm_with_partial_attach:--enable-partial-attach} \
	%{?with_db2_dir:--with-db2-dir=%{?with_db2_dir}} \
	%{?with_pam_dir:--with-pam_dir=%{?with_pam_dir}} \
	%{?with_proctrack:--with-proctrack=%{?with_proctrack}}\
	%{?with_cpusetdir:--with-cpusetdir=%{?with_cpusetdir}} \
	%{?with_apbasildir:--with-apbasildir=%{?with_apbasildir}} \
	%{?with_mysql_config:--with-mysql_config=%{?with_mysql_config}} \
	%{?with_pg_config:--with-pg_config=%{?with_pg_config}} \
	%{?with_ssl:--with-ssl=%{?with_ssl}} \
	%{?with_munge:--with-munge=%{?with_munge}}\
	%{?with_netloc:--with-netloc=%{?with_netloc}}\
	%{?with_blcr:--with-blcr=%{?with_blcr}}\
	%{?slurm_with_cray:--enable-native-cray}\
	%{?slurm_with_cray_network:--enable-cray-network}\
	%{?slurm_with_salloc_background:--enable-salloc-background} \
	%{!?slurm_with_readline:--without-readline} \
	%{?slurm_with_multiple_slurmd:--enable-multiple-slurmd} \
	%{?slurm_with_pmix:--with-pmix=%{?slurm_with_pmix}} \
	%{?with_freeipmi:--with-freeipmi=%{?with_freeipmi}}\
	%{?with_cflags}

%__make %{?_smp_mflags}

%install


# Strip out some dependencies

cat > find-requires.sh <<'EOF'
exec %{__find_requires} "$@" | egrep -v '^libpmix.so|libevent'
EOF
chmod +x find-requires.sh
%global _use_internal_dependency_generator 0
%global __find_requires %{_builddir}/%{buildsubdir}/find-requires.sh


rm -rf "$RPM_BUILD_ROOT"
DESTDIR="$RPM_BUILD_ROOT" %__make install
DESTDIR="$RPM_BUILD_ROOT" %__make install-contrib

if [ -d /usr/lib/systemd/system ]; then
   install -D -m644 etc/slurmctld.service $RPM_BUILD_ROOT/usr/lib/systemd/system/slurmctld.service
   install -D -m644 etc/slurmd.service    $RPM_BUILD_ROOT/usr/lib/systemd/system/slurmd.service
   install -D -m644 etc/slurmdbd.service  $RPM_BUILD_ROOT/usr/lib/systemd/system/slurmdbd.service
elif [ -d /etc/init.d ]; then
   install -D -m755 etc/init.d.slurm    $RPM_BUILD_ROOT/etc/init.d/slurm
   install -D -m755 etc/init.d.slurmdbd $RPM_BUILD_ROOT/etc/init.d/slurmdbd
   mkdir -p "$RPM_BUILD_ROOT/usr/sbin"
   ln -s ../../etc/init.d/slurm    $RPM_BUILD_ROOT/usr/sbin/rcslurm
   ln -s ../../etc/init.d/slurmdbd $RPM_BUILD_ROOT/usr/sbin/rcslurmdbd
fi

# Do not package Slurm's version of libpmi on Cray systems.
# Cray's version of libpmi should be used.
%if %{slurm_with cray} || %{slurm_with cray_alps}
   rm -f $RPM_BUILD_ROOT/%{_libdir}/libpmi*
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
test -s $RPM_BUILD_ROOT/%{_perldir}/auto/Slurm/Slurm.bs         ||
rm   -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurm/Slurm.bs

test -s $RPM_BUILD_ROOT/%{_perldir}/auto/Slurmdb/Slurmdb.bs     ||
rm   -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurmdb/Slurmdb.bs

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
%if ! %{slurm_with auth_none}
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/auth_none.so
%endif
%if ! %{slurm_with bluegene}
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/libsched_if.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/libsched_if64.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/runjob_plugin.so
rm -f $RPM_BUILD_ROOT/%{_mandir}/man5/bluegene*
rm -f $RPM_BUILD_ROOT/%{_sbindir}/sfree
rm -f $RPM_BUILD_ROOT/%{_sbindir}/slurm_epilog
rm -f $RPM_BUILD_ROOT/%{_sbindir}/slurm_prolog
%endif
%if ! %{slurm_with munge}
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/auth_munge.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/crypto_munge.so
%endif
rm -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurm/.packlist
rm -f $RPM_BUILD_ROOT/%{_perlarchlibdir}/perllocal.pod
rm -f $RPM_BUILD_ROOT/%{_perldir}/perllocal.pod
rm -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurmdb/.packlist

%if ! %{slurm_with blcr}
# remove these if they exist
rm -f ${RPM_BUILD_ROOT}%{_mandir}/man1/srun_cr*
rm -f ${RPM_BUILD_ROOT}%{_bindir}/srun_cr
rm -f ${RPM_BUILD_ROOT}%{_libdir}/slurm/checkpoint_blcr.so
rm -f ${RPM_BUILD_ROOT}%{_libexecdir}/slurm/cr_*
%endif

%if ! %{slurm_with lua}
rm -f ${RPM_BUILD_ROOT}%{_libdir}/slurm/job_submit_lua.so
rm -f ${RPM_BUILD_ROOT}%{_libdir}/slurm/proctrack_lua.so
%endif

%if ! %{slurm_with sgijob}
rm -f ${RPM_BUILD_ROOT}%{_libdir}/slurm/proctrack_sgi_job.so
%endif

%if ! %{slurm_with percs}
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/launch_poe.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/libpermapi.so
rm -f ${RPM_BUILD_ROOT}%{_libdir}/slurm/switch_nrt.so
%endif

# Build man pages that are generated directly by the tools
rm -f $RPM_BUILD_ROOT/%{_mandir}/man1/sjobexitmod.1
${RPM_BUILD_ROOT}%{_bindir}/sjobexitmod --roff > $RPM_BUILD_ROOT/%{_mandir}/man1/sjobexitmod.1
rm -f $RPM_BUILD_ROOT/%{_mandir}/man1/sjstat.1
${RPM_BUILD_ROOT}%{_bindir}/sjstat --roff > $RPM_BUILD_ROOT/%{_mandir}/man1/sjstat.1

# Build conditional file list for main package
LIST=./slurm.files
touch $LIST
test -f $RPM_BUILD_ROOT/etc/init.d/slurm			&&
  echo /etc/init.d/slurm				>> $LIST
test -f $RPM_BUILD_ROOT/%{_libexecdir}/slurm/cr_checkpoint.sh   &&
  echo %{_libexecdir}/slurm/cr_checkpoint.sh	        >> $LIST
test -f $RPM_BUILD_ROOT/%{_libexecdir}/slurm/cr_restart.sh      &&
  echo %{_libexecdir}/slurm/cr_restart.sh	        >> $LIST
test -f $RPM_BUILD_ROOT/%{_sbindir}/capmc_suspend		&&
  echo %{_sbindir}/capmc_suspend			>> $LIST
test -f $RPM_BUILD_ROOT/%{_sbindir}/capmc_resume		&&
  echo %{_sbindir}/capmc_resume				>> $LIST
test -f $RPM_BUILD_ROOT/usr/sbin/rcslurm			&&
  echo /usr/sbin/rcslurm				>> $LIST
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

%if %{slurm_with bluegene}
install -D -m644 etc/bluegene.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/bluegene.conf.example

LIST=./bluegene.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/libsched_if.so &&
   echo %{_libdir}/slurm/libsched_if.so >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/libsched_if64.so &&
   echo %{_libdir}/slurm/libsched_if64.so >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/runjob_plugin.so &&
   echo %{_libdir}/slurm/runjob_plugin.so >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/launch_runjob.so &&
   echo %{_libdir}/slurm/launch_runjob.so >> $LIST

%endif

LIST=./percs.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/checkpoint_poe.so	&&
   echo %{_libdir}/slurm/checkpoint_poe.so		 >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/switch_nrt.so  	&&
  echo %{_libdir}/slurm/switch_nrt.so			>> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/libpermapi.so  	&&
  echo %{_libdir}/slurm/libpermapi.so			>> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/launch_poe.so          &&
   echo %{_libdir}/slurm/launch_poe.so                  >> $LIST


LIST=./slurmdbd.files
touch $LIST
test -f $RPM_BUILD_ROOT/etc/init.d/slurmdbd			&&
  echo /etc/init.d/slurmdbd				>> $LIST
test -f $RPM_BUILD_ROOT/usr/sbin/rcslurmdbd			&&
  echo /usr/sbin/rcslurmdbd				>> $LIST
test -f $RPM_BUILD_ROOT/usr/lib/systemd/system/slurmdbd.service	&&
  echo /usr/lib/systemd/system/slurmdbd.service		>> $LIST

LIST=./sql.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/accounting_storage_mysql.so &&
   echo %{_libdir}/slurm/accounting_storage_mysql.so >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/jobcomp_mysql.so            &&
   echo %{_libdir}/slurm/jobcomp_mysql.so            >> $LIST

LIST=./plugins.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/acct_gather_energy_cray.so  &&
   echo %{_libdir}/slurm/acct_gather_energy_cray.so  >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/acct_gather_energy_ibmaem.so &&
   echo %{_libdir}/slurm/acct_gather_energy_ibmaem.so  >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/acct_gather_energy_ipmi.so  &&
   echo %{_libdir}/slurm/acct_gather_energy_ipmi.so  >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/acct_gather_energy_rapl.so  &&
   echo %{_libdir}/slurm/acct_gather_energy_rapl.so  >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/acct_gather_infiniband_ofed.so &&
   echo %{_libdir}/slurm/acct_gather_infiniband_ofed.so >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/acct_gather_profile_hdf5.so &&
   echo %{_libdir}/slurm/acct_gather_profile_hdf5.so >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/burst_buffer_cray.so        &&
   echo %{_libdir}/slurm/burst_buffer_cray.so        >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/checkpoint_blcr.so          &&
   echo %{_libdir}/slurm/checkpoint_blcr.so          >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/crypto_openssl.so           &&
   echo %{_libdir}/slurm/crypto_openssl.so           >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/ext_sensors_rrd.so          &&
   echo %{_libdir}/slurm/ext_sensors_rrd.so          >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/jobcomp_elasticsearch.so    &&
   echo %{_libdir}/slurm/jobcomp_elasticsearch.so    >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/launch_slurm.so             &&
   echo %{_libdir}/slurm/launch_slurm.so             >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/launch_aprun.so             &&
   echo %{_libdir}/slurm/launch_aprun.so             >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/mpi_mvapich.so              &&
   echo %{_libdir}/slurm/mpi_mvapich.so              >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/mpi_pmix.so                 &&
   echo %{_libdir}/slurm/mpi_pmix.so                 >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/mpi_pmix_v1.so              &&
   echo %{_libdir}/slurm/mpi_pmix_v1.so              >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/mpi_pmix_v2.so              &&
   echo %{_libdir}/slurm/mpi_pmix_v2.so              >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/node_features_knl_cray.so   &&
   echo %{_libdir}/slurm/node_features_knl_cray.so   >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/node_features_knl_generic.so &&
   echo %{_libdir}/slurm/node_features_knl_generic.so   >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/power_cray.so               &&
   echo %{_libdir}/slurm/power_cray.so               >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/select_bluegene.so          &&
   echo %{_libdir}/slurm/select_bluegene.so          >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/task_affinity.so            &&
   echo %{_libdir}/slurm/task_affinity.so            >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/task_cgroup.so              &&
   echo %{_libdir}/slurm/task_cgroup.so              >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/job_container_cncu.so       &&
   echo %{_libdir}/slurm/job_container_cncu.so       >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/job_container_none.so       &&
   echo %{_libdir}/slurm/job_container_none.so       >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/task_cray.so                &&
   echo %{_libdir}/slurm/task_cray.so                >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/slurmctld_nonstop.so        &&
   echo %{_libdir}/slurm/slurmctld_nonstop.so        >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/switch_cray.so              &&
   echo %{_libdir}/slurm/switch_cray.so              >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/proctrack_cray.so           &&
   echo %{_libdir}/slurm/proctrack_cray.so           >> $LIST

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
%if %{slurm_with cray} || %{slurm_with cray_alps}
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

%if %{slurm_with auth_none}
%files auth-none
%defattr(-,root,root)
%{_libdir}/slurm/auth_none.so
%endif
#############################################################################

%if %{slurm_with munge}
%files munge
%defattr(-,root,root)
%{_libdir}/slurm/auth_munge.so
%{_libdir}/slurm/crypto_munge.so
%endif
#############################################################################

%if %{slurm_with bluegene}
%files -f bluegene.files bluegene
%defattr(-,root,root)
%{_mandir}/man5/bluegene.*
%{_sbindir}/slurm_epilog
%{_sbindir}/slurm_prolog
%{_sbindir}/sfree
%config %{_sysconfdir}/bluegene.conf.example
%endif
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
%{_mandir}/man5/slurmdbd.*
%{_mandir}/man8/slurmdbd.*
%config %{_sysconfdir}/slurmdbd.conf.example
#############################################################################

%files -f sql.files sql
%defattr(-,root,root)
%dir %{_libdir}/slurm
#############################################################################

%files -f plugins.files plugins
%defattr(-,root,root)
%dir %{_libdir}/slurm
%{_libdir}/slurm/accounting_storage_filetxt.so
%{_libdir}/slurm/accounting_storage_none.so
%{_libdir}/slurm/accounting_storage_slurmdbd.so
%{_libdir}/slurm/acct_gather_filesystem_lustre.so
%{_libdir}/slurm/acct_gather_filesystem_none.so
%{_libdir}/slurm/acct_gather_infiniband_none.so
%{_libdir}/slurm/acct_gather_energy_none.so
%{_libdir}/slurm/acct_gather_profile_none.so
%{_libdir}/slurm/burst_buffer_generic.so
%{_libdir}/slurm/checkpoint_none.so
%{_libdir}/slurm/checkpoint_ompi.so
%{_libdir}/slurm/core_spec_cray.so
%{_libdir}/slurm/core_spec_none.so
%{_libdir}/slurm/ext_sensors_none.so
%{_libdir}/slurm/gres_gpu.so
%{_libdir}/slurm/gres_mic.so
%{_libdir}/slurm/gres_nic.so
%{_libdir}/slurm/job_submit_all_partitions.so
%{_libdir}/slurm/job_submit_cray.so
%{_libdir}/slurm/job_submit_require_timelimit.so
%{_libdir}/slurm/job_submit_throttle.so
%{_libdir}/slurm/jobacct_gather_cgroup.so
%{_libdir}/slurm/jobacct_gather_linux.so
%{_libdir}/slurm/jobacct_gather_none.so
%{_libdir}/slurm/jobcomp_filetxt.so
%{_libdir}/slurm/jobcomp_none.so
%{_libdir}/slurm/jobcomp_script.so
%{_libdir}/slurm/layouts_power_cpufreq.so
%{_libdir}/slurm/layouts_power_default.so
%{_libdir}/slurm/layouts_unit_default.so
%{_libdir}/slurm/mcs_account.so
%{_libdir}/slurm/mcs_group.so
%{_libdir}/slurm/mcs_none.so
%{_libdir}/slurm/mcs_user.so
%if ! %{slurm_with bluegene}
%{_libdir}/slurm/mpi_lam.so
%{_libdir}/slurm/mpi_mpich1_p4.so
%{_libdir}/slurm/mpi_mpich1_shmem.so
%{_libdir}/slurm/mpi_mpichgm.so
%{_libdir}/slurm/mpi_mpichmx.so
%{_libdir}/slurm/mpi_openmpi.so
%{_libdir}/slurm/mpi_pmi2.so
%endif
%{_libdir}/slurm/mpi_none.so
%{_libdir}/slurm/power_none.so
%{_libdir}/slurm/preempt_job_prio.so
%{_libdir}/slurm/preempt_none.so
%{_libdir}/slurm/preempt_partition_prio.so
%{_libdir}/slurm/preempt_qos.so
%{_libdir}/slurm/priority_basic.so
%{_libdir}/slurm/priority_multifactor.so
%{_libdir}/slurm/proctrack_cgroup.so
%{_libdir}/slurm/proctrack_linuxproc.so
%{_libdir}/slurm/proctrack_pgid.so
%{_libdir}/slurm/route_default.so
%{_libdir}/slurm/route_topology.so
%{_libdir}/slurm/sched_backfill.so
%{_libdir}/slurm/sched_builtin.so
%{_libdir}/slurm/sched_hold.so
%{_libdir}/slurm/select_alps.so
%{_libdir}/slurm/select_cray.so
%{_libdir}/slurm/select_cons_res.so
%{_libdir}/slurm/select_linear.so
%{_libdir}/slurm/select_serial.so
%{_libdir}/slurm/switch_generic.so
%{_libdir}/slurm/switch_none.so
%{_libdir}/slurm/task_none.so
%{_libdir}/slurm/topology_3d_torus.so
%{_libdir}/slurm/topology_hypercube.so
%{_libdir}/slurm/topology_node_rank.so
%{_libdir}/slurm/topology_none.so
%{_libdir}/slurm/topology_tree.so
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

%if %{slurm_with percs}
%files -f percs.files percs
%defattr(-,root,root)
%endif
#############################################################################

%if %{slurm_with sgijob}
%files proctrack-sgi-job
%defattr(-,root,root)
%{_libdir}/slurm/proctrack_sgi_job.so
%endif
#############################################################################

%if %{slurm_with lua}
%files lua
%defattr(-,root,root)
%doc contribs/lua/proctrack.lua
%{_libdir}/slurm/job_submit_lua.so
%{_libdir}/slurm/proctrack_lua.so
%endif
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
#if [ -x /etc/init.d/slurm ]; then
#    if /etc/init.d/slurm status | grep -q running; then
#        /etc/init.d/slurm stop
#    fi
#fi
#if [ -x /etc/init.d/slurmdbd ]; then
#    if /etc/init.d/slurmdbd status | grep -q running; then
#        /etc/init.d/slurmdbd stop
#    fi
#fi

%post
if [ -x /sbin/ldconfig ]; then
    /sbin/ldconfig %{_libdir}
    if [ $1 = 1 ]; then
	if [ -x /etc/init.d/slurm ]; then
	    [ -x /sbin/chkconfig ] && /sbin/chkconfig --add slurm
        fi
    fi
fi

%if %{slurm_with bluegene}
%post bluegene
if [ -x /sbin/ldconfig ]; then
    /sbin/ldconfig %{_libdir}/slurm
fi
%endif

%preun
if [ "$1" -eq 0 ]; then
    if [ -x /etc/init.d/slurm ]; then
	[ -x /sbin/chkconfig ] && /sbin/chkconfig --del slurm
	if /etc/init.d/slurm status | grep -q running; then
	    /etc/init.d/slurm stop
	fi
    fi
fi

%preun slurmdbd
if [ "$1" -eq 0 ]; then
    if [ -x /etc/init.d/slurmdbd ]; then
	[ -x /sbin/chkconfig ] && /sbin/chkconfig --del slurmdbd
	if /etc/init.d/slurmdbd status | grep -q running; then
	    /etc/init.d/slurmdbd stop
	fi
    fi
fi

%postun
if [ "$1" -gt 1 ]; then
    if [ -x /etc/init.d/slurmdbd ]; then
        /etc/init.d/slurm condrestart
    fi
elif [ "$1" -eq 0 ]; then
    if [ -x /sbin/ldconfig ]; then
	/sbin/ldconfig %{_libdir}
    fi
fi
%if %{?insserv_cleanup:1}0
%insserv_cleanup
%endif

%postun slurmdbd
if [ "$1" -gt 1 ]; then
    if [ -x /etc/init.d/slurmdbd ]; then
        /etc/init.d/slurm condrestart
    fi
fi

#############################################################################


%changelog
* Wed Jun 26 2013 Morris Jette <jette@schedmd.com> 14.03.0-0pre1
Various cosmetic fixes for rpmlint errors
