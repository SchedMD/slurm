# $Id$
#
# Note that this package is not relocatable

#
# build options      .rpmmacros options      change to default action
# ===============    ====================    ========================
# --enable-multiple-slurmd %_with_multiple_slurmd 1 build with the multiple slurmd option.  Typically used to simulate a larger system than one has access to.
# --enable-salloc-background %_with_salloc_background 1 on a cray system alloc salloc to execute as a background process.
# --prefix           %_prefix        path    install path for commands, libraries, etc.
# --with aix         %_with_aix         1    build aix RPM
# --with authd       %_with_authd       1    build auth-authd RPM
# --with auth_none   %_with_auth_none   1    build auth-none RPM
# --with blcr        %_with_blcr        1    require blcr support
# --with bluegene    %_with_bluegene    1    build bluegene RPM
# --with cray        %_with_cray        1    build for a Cray system without ALPS
# --with cray_alps   %_with_cray_alps   1    build for a Cray system with ALPS
# --with debug       %_with_debug       1    enable extra debugging within SLURM
# --with lua         %_with_lua         1    build SLURM lua bindings (proctrack only for now)
# --without munge    %_without_munge    1    don't build auth-munge RPM
# --with mysql       %_with_mysql       1    require mysql support
# --with openssl     %_with_openssl     1    require openssl RPM to be installed
# --without pam      %_without_pam      1    don't require pam-devel RPM to be installed
# --with percs       %_with_percs       1    build percs RPM
# --with postgres    %_with_postgres    1    require postgresql support
# --without readline %_without_readline 1    don't require readline-devel RPM to be installed
# --with sgijob      %_with_sgijob      1    build proctrack-sgi-job RPM
# --with sun_const   %_with_sun_const   1    build for Sun Constellation system
#
#  Allow defining --with and --without build options or %_with and %without in .rpmmacors
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
%slurm_without_opt authd
%slurm_without_opt bluegene
%slurm_without_opt cray
%slurm_without_opt cray_alps
%slurm_without_opt debug
%slurm_without_opt sun_const
%slurm_without_opt salloc_background
%slurm_without_opt multiple_slurmd

# These options are only here to force there to be these on the build.
# If they are not set they will still be compiled if the packages exist.
%slurm_without_opt mysql
%slurm_without_opt postgres
%slurm_without_opt blcr
%slurm_without_opt openssl

# Build with munge by default on all platforms (disable using --without munge)
%slurm_with_opt munge

# Build with OpenSSL by default on all platforms (disable using --without openssl)
%slurm_with_opt openssl

# Use readline by default on all systems
%slurm_with_opt readline

# Build with PAM by default on linux
%ifos linux
%slurm_with_opt pam
%endif

# Define with_aix on AIX systems (for proctrack)
%ifos aix5.3
%slurm_with_opt aix
%endif

# Build with sgijob plugin and mysql (for slurmdbd) on CHAOS systems
%if %{?chaos}0
%slurm_with_opt mysql
%slurm_with_opt lua
%slurm_with_opt partial_attach
%else
%slurm_without_opt sgijob
%slurm_without_opt lua
%slurm_without_opt partial-attach
%endif

%if %{?chaos}0 && 0%{?chaos} < 5
%slurm_with_opt sgijob
%endif

%if %{slurm_with cray_alps}
%slurm_with_opt sgijob
%endif

Name:    see META file
Version: see META file
Release: see META file

Summary: Simple Linux Utility for Resource Management

License: GPL
Group: System Environment/Base
Source: %{name}-%{version}-%{release}.tgz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
URL: http://slurm.schedmd.com/

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

%if %{?chaos}0
BuildRequires: gtk2-devel >= 2.7.1
BuildRequires: ncurses-devel
BuildRequires: pkgconfig
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

%if %{slurm_with mysql}
BuildRequires: mysql-devel >= 5.0.0
%endif

%if %{slurm_with postgres}
BuildRequires: postgresql-devel >= 8.0.0
%endif

%if %{slurm_with cray_alps}
BuildRequires: cray-MySQL-devel-enterprise
Requires: cray-MySQL-devel-enterprise
%endif

%if %{slurm_with cray}
BuildRequires: cray-MySQL-devel-enterprise
BuildRequires: cray-libalpscomm_cn-devel
BuildRequires: cray-libalpscomm_sn-devel
BuildRequires: cray-libjob-devel
%endif

%ifnos aix5.3
# FIXME: AIX can't seem to find this even though this is in existance there.
# We should probably figure out a better way of doing this, but for now we
# just won't look for it on AIX.
BuildRequires: perl(ExtUtils::MakeMaker)
%endif

%description
SLURM is an open source, fault-tolerant, and highly
scalable cluster management and job scheduling system for Linux clusters
containing up to 65,536 nodes. Components include machine status,
partition management, job management, scheduling and accounting modules

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

# AIX doesn't always give the correct install prefix here for mans 
%ifos aix5.3
%define _perlman3 %(perl -e 'use Config; $T=$Config{installsiteman3dir}; $P=$Config{siteprefix}; $P1="$P/local"; $T =~ s/$P1//; $T =~ s/$P//; $P="/usr/share"; $T =~ s/$P//; print $T;')
%else
%define _perlman3 %(perl -e 'use Config; $T=$Config{installsiteman3dir}; $P=$Config{siteprefix}; $P1="$P/local"; $T =~ s/$P1//; $T =~ s/$P//; print $T;')
%endif

%define _perlarchlib %(perl -e 'use Config; $T=$Config{installarchlib}; $P=$Config{installprefix}; $P1="$P/local"; $T =~ s/$P1//; $T =~ s/$P//; print $T;')

%define _perldir %{_prefix}%{_perlarch}
%define _perlman3dir %{_prefix}%{_perlman3}
%define _perlarchlibdir %{_prefix}%{_perlarchlib}
%define _php_extdir %(php-config --extension-dir 2>/dev/null || echo %{_libdir}/php5)

%package perlapi
Summary: Perl API to SLURM
Group: Development/System
Requires: slurm
%description perlapi
Perl API package for SLURM.  This package includes the perl API to provide a
helpful interface to SLURM through Perl

%package devel
Summary: Development package for SLURM
Group: Development/System
Requires: slurm
%description devel
Development package for SLURM.  This package includes the header files
and static libraries for the SLURM API

%if %{slurm_with auth_none}
%package auth-none
Summary: SLURM auth NULL implementation (no authentication)
Group: System Environment/Base
Requires: slurm
%description auth-none
SLURM NULL authentication module
%endif

%if %{slurm_with authd}
%package auth-authd
Summary: SLURM auth implementation using Brent Chun's authd
Group: System Environment/Base
Requires: slurm authd
%description auth-authd
SLURM authentication module for Brent Chun's authd. Used to
authenticate user originating an RPC
%endif

# This is named munge instead of auth-munge since there are 2 plugins in the
# package.  auth-munge and crypto-munge
%if %{slurm_with munge}
%package munge
Summary: SLURM authentication and crypto implementation using Munge
Group: System Environment/Base
Requires: slurm munge
BuildRequires: munge-devel munge-libs
Obsoletes: slurm-auth-munge
%description munge
SLURM authentication and crypto implementation using Munge. Used to
authenticate user originating an RPC, digitally sign and/or encrypt messages
%endif

%if %{slurm_with bluegene}
%package bluegene
Summary: SLURM interfaces to IBM Blue Gene system
Group: System Environment/Base
Requires: slurm
%description bluegene
SLURM plugin interfaces to IBM Blue Gene system
%endif

%package slurmdbd
Summary: SLURM database daemon
Group: System Environment/Base
Requires: slurm-plugins slurm-sql
%description slurmdbd
SLURM database daemon. Used to accept and process database RPCs and upload
database changes to slurmctld daemons on each cluster

%package sql
Summary: SLURM SQL support
Group: System Environment/Base
%description sql
SLURM SQL support. Contains interfaces to MySQL and PostGreSQL

%package plugins
Summary: SLURM plugins (loadable shared objects)
Group: System Environment/Base
%description plugins
SLURM plugins (loadable shared objects) supporting a wide variety of
architectures and behaviors. These basically provide the building blocks
with which Slurm can be configured. Note that some system specific plugins
are in other packages

%package torque
Summary: Torque/PBS wrappers for transitition from Torque/PBS to SLURM
Group: Development/System
Requires: slurm-perlapi
%description torque
Torque wrapper scripts used for helping migrate from Torque/PBS to SLURM

%package sjobexit
Summary: SLURM job exit code management tools
Group: Development/System
Requires: slurm-perlapi
%description sjobexit
SLURM job exit code management tools. Enables users to alter job exit code
information for completed jobs

%package slurmdb-direct
Summary: Wrappers to write directly to the slurmdb
Group: Development/System
Requires: slurm-perlapi
%description slurmdb-direct
Wrappers to write directly to the slurmdb

%if %{slurm_with aix}
%package aix
Summary: SLURM interfaces to IBM AIX
Group: System Environment/Base
Requires: slurm
BuildRequires: proctrack >= 3
Obsoletes: slurm-aix-federation
%description aix
SLURM interfaces for IBM AIX systems
%endif

%if %{slurm_with percs}
%package percs
Summary: SLURM plugins to run on an IBM PERCS system
Group: System Environment/Base
Requires: slurm nrt
BuildRequires: nrt
%description percs
SLURM plugins to run on an IBM PERCS system, POE interface and NRT switch plugin
%endif


%if %{slurm_with sgijob}
%package proctrack-sgi-job
Summary: SLURM process tracking plugin for SGI job containers
Group: System Environment/Base
Requires: slurm
BuildRequires: job
%description proctrack-sgi-job
SLURM process tracking plugin for SGI job containers
(See http://oss.sgi.com/projects/pagg)
%endif

%if %{slurm_with lua}
%package lua
Summary: SLURM lua bindings
Group: System Environment/Base
Requires: slurm lua
BuildRequires: lua-devel
%description lua
SLURM lua bindings
Includes the SLURM proctrack/lua and job_submit/lua plugin
%endif

%package sjstat
Summary: Perl tool to print SLURM job state information
Group: Development/System
Requires: slurm
%description sjstat
Perl tool to print SLURM job state information. The output is designed to give
information on the resource usage and availablilty, as well as information
about jobs that are currently active on the machine. This output is built
using the SLURM utilities, sinfo, squeue and scontrol, the man pages for these
utilites will provide more information and greater depth of understanding

%if %{slurm_with pam}
%package pam_slurm
Summary: PAM module for restricting access to compute nodes via SLURM
Group: System Environment/Base
Requires: slurm slurm-devel
BuildRequires: pam-devel
Obsoletes: pam_slurm
%description pam_slurm
This module restricts access to compute nodes in a cluster where the Simple
Linux Utility for Resource Managment (SLURM) is in use.  Access is granted
to root, any user with an SLURM-launched job currently running on the node,
or any user who has allocated resources on the node according to the SLURM
%endif

%if %{slurm_with blcr}
%package blcr
Summary: Allows SLURM to use Berkeley Lab Checkpoint/Restart
Group: System Environment/Base
Requires: slurm
%description blcr
Gives the ability for SLURM to use Berkeley Lab Checkpoint/Restart
%endif

#############################################################################

%prep
%setup -n %{name}-%{version}-%{release}

%build
%configure \
	%{?slurm_with_debug:--enable-debug} \
	%{?slurm_with_partial_attach:--enable-partial-attach} \
	%{?slurm_with_sun_const:--enable-sun-const} \
	%{?with_db2_dir} \
	%{?with_pam_dir}	\
	%{?with_proctrack}	\
	%{?with_cpusetdir} \
	%{?with_apbasildir} \
	%{?with_xcpu} \
	%{?with_mysql_config} \
	%{?with_pg_config} \
	%{?with_ssl}		\
	%{?with_munge}      \
	%{?with_blcr}      \
	%{?slurm_with_cray:--enable-native-cray}      \
	%{?slurm_with_salloc_background:--enable-salloc-background} \
	%{!?slurm_with_readline:--without-readline} \
	%{?slurm_with_multiple_slurmd:--enable-multiple-slurmd} \
	%{?with_cflags}

make %{?_smp_mflags}

%install
rm -rf "$RPM_BUILD_ROOT"
DESTDIR="$RPM_BUILD_ROOT" make install
DESTDIR="$RPM_BUILD_ROOT" make install-contrib

%ifos aix5.3
   mv ${RPM_BUILD_ROOT}%{_bindir}/srun ${RPM_BUILD_ROOT}%{_sbindir}
%else
   if [ -d /etc/init.d ]; then
      install -D -m755 etc/init.d.slurm    $RPM_BUILD_ROOT/etc/init.d/slurm
      install -D -m755 etc/init.d.slurmdbd $RPM_BUILD_ROOT/etc/init.d/slurmdbd
      mkdir -p "$RPM_BUILD_ROOT/usr/sbin"
      ln -s ../../etc/init.d/slurm    $RPM_BUILD_ROOT/usr/sbin/rcslurm
      ln -s ../../etc/init.d/slurmdbd $RPM_BUILD_ROOT/usr/sbin/rcslurmdbd
   fi
%endif

# Do not package Slurm's version of libpmi on Cray systems.
# Cray's version of libpmi should be used.
%if %{slurm_with cray} || %{slurm_with cray_alps}
   rm -f $RPM_BUILD_ROOT/%{_libdir}/libpmi*
   if [ -d /opt/modulefiles ]; then
      install -D -m644 contribs/cray/opt_modulefiles_slurm $RPM_BUILD_ROOT/opt/modulefiles/slurm/opt_modulefiles_slurm
   fi
%else
   rm -f contribs/cray/opt_modulefiles_slurm
%endif

install -D -m644 etc/slurm.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/slurm.conf.example
install -D -m644 etc/cgroup.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/cgroup.conf.example
install -D -m644 etc/cgroup_allowed_devices_file.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/cgroup_allowed_devices_file.conf.example
install -D -m755 etc/cgroup.release_common.example ${RPM_BUILD_ROOT}%{_sysconfdir}/cgroup.release_common.example
install -D -m755 etc/cgroup.release_common.example ${RPM_BUILD_ROOT}%{_sysconfdir}/cgroup/release_freezer
install -D -m755 etc/cgroup.release_common.example ${RPM_BUILD_ROOT}%{_sysconfdir}/cgroup/release_cpuset
install -D -m755 etc/cgroup.release_common.example ${RPM_BUILD_ROOT}%{_sysconfdir}/cgroup/release_memory
install -D -m644 etc/slurmdbd.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/slurmdbd.conf.example
install -D -m755 etc/slurm.epilog.clean ${RPM_BUILD_ROOT}%{_sysconfdir}/slurm.epilog.clean
install -D -m755 contribs/sgather/sgather ${RPM_BUILD_ROOT}%{_bindir}/sgather
install -D -m755 contribs/sjstat ${RPM_BUILD_ROOT}%{_bindir}/sjstat

# Correct some file permissions
test -f $RPM_BUILD_ROOT/%{_libdir}/libpmi.la	&&
	chmod 644 $RPM_BUILD_ROOT/%{_libdir}/libpmi.la
test -f $RPM_BUILD_ROOT/%{_libdir}/libslurm.la	&&
	chmod 644 $RPM_BUILD_ROOT/%{_libdir}/libslurm.la
test -f $RPM_BUILD_ROOT/%{_libdir}/libslurmdb.la &&
	chmod 644 $RPM_BUILD_ROOT/%{_libdir}/libslurmdb.la

# Delete unpackaged files:
test -s $RPM_BUILD_ROOT/%{_perldir}/auto/Slurm/Slurm.bs         ||
rm   -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurm/Slurm.bs

test -s $RPM_BUILD_ROOT/%{_perldir}/auto/Slurmdb/Slurmdb.bs     ||
rm   -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurmdb/Slurmdb.bs

rm -f $RPM_BUILD_ROOT/%{_libdir}/libpmi.a
rm -f $RPM_BUILD_ROOT/%{_libdir}/libpmi2.a
rm -f $RPM_BUILD_ROOT/%{_libdir}/libslurm.a
rm -f $RPM_BUILD_ROOT/%{_libdir}/libslurmdb.a
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/*.a
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/*.la
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/job_submit_defaults.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/job_submit_logging.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/job_submit_partition.so
rm -f $RPM_BUILD_ROOT/%{_libdir}/security/*.a
rm -f $RPM_BUILD_ROOT/%{_libdir}/security/*.la
%if %{?with_pam_dir}0
rm -f $RPM_BUILD_ROOT/%{with_pam_dir}/pam_slurm.la
rm -f $RPM_BUILD_ROOT/%{with_pam_dir}/pam_slurm.la
%endif
rm -f $RPM_BUILD_ROOT/lib/security/pam_slurm.a
rm -f $RPM_BUILD_ROOT/lib/security/pam_slurm.la
rm -f $RPM_BUILD_ROOT/lib32/security/pam_slurm.a
rm -f $RPM_BUILD_ROOT/lib32/security/pam_slurm.la
rm -f $RPM_BUILD_ROOT/lib64/security/pam_slurm.a
rm -f $RPM_BUILD_ROOT/lib64/security/pam_slurm.la
%if ! %{slurm_with auth_none}
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/auth_none.so
%endif
%if ! %{slurm_with bluegene}
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/job_submit_cnode.so
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
test -f $RPM_BUILD_ROOT/usr/sbin/rcslurm			&&
  echo /usr/sbin/rcslurm				>> $LIST

test -f $RPM_BUILD_ROOT/opt/modulefiles/slurm/opt_modulefiles_slurm &&
  echo /opt/modulefiles/slurm/opt_modulefiles_slurm	>> $LIST

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

LIST=./aix.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/proctrack_aix.so      &&
  echo %{_libdir}/slurm/proctrack_aix.so               >> $LIST

LIST=./devel.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/libpmi.la			&&
  echo %{_libdir}/libpmi.la				>> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/libpmi2.la			&&
  echo %{_libdir}/libpmi2.la				>> $LIST

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

LIST=./sql.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/accounting_storage_mysql.so &&
   echo %{_libdir}/slurm/accounting_storage_mysql.so >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/jobcomp_mysql.so            &&
   echo %{_libdir}/slurm/jobcomp_mysql.so            >> $LIST

LIST=./perlapi.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurm/Slurm.bs        &&
   echo $RPM_BUILD_ROOT/%{_perldir}/auto/Slurm/Slurm.bs        >> $LIST
test -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurmdb/Slurmdb.bs    &&
   echo $RPM_BUILD_ROOT/%{_perldir}/auto/Slurmdb/Slurmdb.bs    >> $LIST

LIST=./plugins.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/acct_gather_energy_ipmi.so  &&
   echo %{_libdir}/slurm/acct_gather_energy_ipmi.so  >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/acct_gather_energy_rapl.so  &&
   echo %{_libdir}/slurm/acct_gather_energy_rapl.so  >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/acct_gather_infiniband_ofed.so &&
   echo %{_libdir}/slurm/acct_gather_infiniband_ofed.so >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/acct_gather_profile_hdf5.so &&
   echo %{_libdir}/slurm/acct_gather_profile_hdf5.so >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/crypto_openssl.so           &&
   echo %{_libdir}/slurm/crypto_openssl.so           >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/ext_sensors_rrd.so          &&
   echo %{_libdir}/slurm/ext_sensors_rrd.so          >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/launch_slurm.so             &&
   echo %{_libdir}/slurm/launch_slurm.so             >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/launch_aprun.so             &&
   echo %{_libdir}/slurm/launch_aprun.so             >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/select_bluegene.so          &&
   echo %{_libdir}/slurm/select_bluegene.so          >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/slurmctld_dynalloc.so       &&
   echo %{_libdir}/slurm/slurmctld_dynalloc.so       >> $LIST
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
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/switch_cray.so              &&
   echo %{_libdir}/slurm/switch_cray.so              >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/proctrack_cray.so           &&
   echo %{_libdir}/slurm/proctrack_cray.so           >> $LIST

LIST=./pam.files
touch $LIST
%if %{?with_pam_dir}0
    test -f $RPM_BUILD_ROOT/%{with_pam_dir}/pam_slurm.so	&&
	echo %{with_pam_dir}/pam_slurm.so	>>$LIST
%else
    test -f $RPM_BUILD_ROOT/lib/security/pam_slurm.so		&&
	echo /lib/security/pam_slurm.so		>>$LIST
    test -f $RPM_BUILD_ROOT/lib32/security/pam_slurm.so		&&
	echo /lib32/security/pam_slurm.so	>>$LIST
    test -f $RPM_BUILD_ROOT/lib64/security/pam_slurm.so		&&
	echo /lib64/security/pam_slurm.so	>>$LIST
%endif
#############################################################################

%clean
rm -rf $RPM_BUILD_ROOT
#############################################################################

%files -f slurm.files
%defattr(-,root,root,0755)
%{_datadir}/doc
%{_bindir}/s*
%exclude %{_bindir}/sjobexitmod
%exclude %{_bindir}/sjstat
%{_sbindir}/slurmctld
%{_sbindir}/slurmd
%{_sbindir}/slurmstepd
%ifos aix5.3
%{_sbindir}/srun
%endif
%{_libdir}/*.so*
%{_libdir}/slurm/src/*
%{_mandir}/man1/*
%{_mandir}/man5/acct_gather.*
%{_mandir}/man5/ext_sensors.*
%{_mandir}/man5/cgroup.*
%{_mandir}/man5/cray.*
%{_mandir}/man5/gres.*
%{_mandir}/man5/slurm.*
%{_mandir}/man5/topology.*
%{_mandir}/man5/wiki.*
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
%config %{_sysconfdir}/slurm.conf.example
%config %{_sysconfdir}/cgroup.conf.example
%config %{_sysconfdir}/cgroup_allowed_devices_file.conf.example
%config %{_sysconfdir}/cgroup.release_common.example
%config %{_sysconfdir}/cgroup/release_freezer
%config %{_sysconfdir}/cgroup/release_cpuset
%config %{_sysconfdir}/cgroup/release_memory
%config %{_sysconfdir}/slurm.epilog.clean
%exclude %{_mandir}/man1/sjobexit*
%exclude %{_mandir}/man1/sjstat*
%if %{slurm_with blcr}
%exclude %{_mandir}/man1/srun_cr*
%exclude %{_bindir}/srun_cr
%endif
#############################################################################

%files -f devel.files devel
%defattr(-,root,root)
%dir %attr(0755,root,root)
%dir %{_prefix}/include/slurm
%{_prefix}/include/slurm/*
%{_libdir}/libslurm.la
%{_libdir}/libslurmdb.la
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

%if %{slurm_with authd}
%defattr(-,root,root)
%files auth-authd
%{_libdir}/slurm/auth_authd.so
%endif
#############################################################################

%if %{slurm_with bluegene}
%files -f bluegene.files bluegene
%defattr(-,root,root)
%{_mandir}/man5/bluegene.*
%{_sbindir}/slurm_epilog
%{_sbindir}/slurm_prolog
%{_sbindir}/sfree
%{_libdir}/slurm/job_submit_cnode.so
%config %{_sysconfdir}/bluegene.conf.example
%endif
#############################################################################

%files -f perlapi.files perlapi
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
%{_libdir}/slurm/checkpoint_none.so
%{_libdir}/slurm/checkpoint_ompi.so
%{_libdir}/slurm/ext_sensors_none.so
%{_libdir}/slurm/gres_gpu.so
%{_libdir}/slurm/gres_mic.so
%{_libdir}/slurm/gres_nic.so
%{_libdir}/slurm/job_submit_all_partitions.so
%{_libdir}/slurm/job_submit_require_timelimit.so
%{_libdir}/slurm/jobacct_gather_aix.so
%{_libdir}/slurm/jobacct_gather_cgroup.so
%{_libdir}/slurm/jobacct_gather_linux.so
%{_libdir}/slurm/jobacct_gather_none.so
%{_libdir}/slurm/jobcomp_filetxt.so
%{_libdir}/slurm/jobcomp_none.so
%{_libdir}/slurm/jobcomp_script.so
%if ! %{slurm_with bluegene}
%{_libdir}/slurm/mpi_lam.so
%{_libdir}/slurm/mpi_mpich1_p4.so
%{_libdir}/slurm/mpi_mpich1_shmem.so
%{_libdir}/slurm/mpi_mpichgm.so
%{_libdir}/slurm/mpi_mpichmx.so
%{_libdir}/slurm/mpi_mvapich.so
%{_libdir}/slurm/mpi_openmpi.so
%{_libdir}/slurm/mpi_pmi2.so
%endif
%{_libdir}/slurm/mpi_none.so
%{_libdir}/slurm/preempt_none.so
%{_libdir}/slurm/preempt_partition_prio.so
%{_libdir}/slurm/preempt_qos.so
%{_libdir}/slurm/priority_basic.so
%{_libdir}/slurm/priority_multifactor.so
%{_libdir}/slurm/proctrack_cgroup.so
%{_libdir}/slurm/proctrack_linuxproc.so
%{_libdir}/slurm/proctrack_pgid.so
%{_libdir}/slurm/sched_backfill.so
%{_libdir}/slurm/sched_builtin.so
%{_libdir}/slurm/sched_hold.so
%{_libdir}/slurm/sched_wiki.so
%{_libdir}/slurm/sched_wiki2.so
%{_libdir}/slurm/select_alps.so
%{_libdir}/slurm/select_cray.so
%{_libdir}/slurm/select_cons_res.so
%{_libdir}/slurm/select_linear.so
%{_libdir}/slurm/select_serial.so
%{_libdir}/slurm/switch_generic.so
%{_libdir}/slurm/switch_none.so
%{_libdir}/slurm/task_none.so
%{_libdir}/slurm/topology_3d_torus.so
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

%files sjobexit
%defattr(-,root,root)
%{_bindir}/sjobexitmod
%{_mandir}/man1/sjobexit*
#############################################################################

%files slurmdb-direct
%defattr(-,root,root)
%config (noreplace) %{_perldir}/config.slurmdb.pl
%{_sbindir}/moab_2_slurmdb
#############################################################################

%if %{slurm_with aix}
%files -f aix.files aix
%defattr(-,root,root)
%{_libdir}/slurm/checkpoint_aix.so
%endif
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

%files sjstat
%defattr(-,root,root)
%{_bindir}/sjstat
%{_mandir}/man1/sjstat*
#############################################################################

%if %{slurm_with pam}
%files -f pam.files pam_slurm
%defattr(-,root,root)
%endif
#############################################################################

%if %{slurm_with blcr}
%files blcr
%defattr(-,root,root)
%{_bindir}/srun_cr
%{_libexecdir}/slurm/cr_*
%{_libdir}/slurm/checkpoint_blcr.so
%{_mandir}/man1/srun_cr*
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
	[ -x /sbin/chkconfig ] && /sbin/chkconfig --add slurm
    fi
fi

%if %{slurm_with bluegene}
%post bluegene
if [ -x /sbin/ldconfig ]; then
    /sbin/ldconfig %{_libdir}/slurm
fi
%endif

%preun
if [ "$1" = 0 ]; then
    if [ -x /etc/init.d/slurm ]; then
	[ -x /sbin/chkconfig ] && /sbin/chkconfig --del slurm
	if /etc/init.d/slurm status | grep -q running; then
	    /etc/init.d/slurm stop
	fi
    fi
fi

%preun slurmdbd
if [ "$1" = 0 ]; then
    if [ -x /etc/init.d/slurmdbd ]; then
	[ -x /sbin/chkconfig ] && /sbin/chkconfig --del slurmdbd
	if /etc/init.d/slurmdbd status | grep -q running; then
	    /etc/init.d/slurmdbd stop
	fi
    fi
fi

%postun
if [ "$1" = 0 ]; then
    if [ -x /sbin/ldconfig ]; then
	/sbin/ldconfig %{_libdir}
    fi
fi
%insserv_cleanup
#############################################################################


%changelog
* Wed Jun 26 2013 Morris Jette <jette@schedmd.com> 13.12.0-0pre1
Various cosmetic fixes for rpmlint errors
