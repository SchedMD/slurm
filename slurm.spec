# $Id$
#
# Note that this package is not relocatable

#
# build options      .rpmmacros options      change to default action
# ===============    ====================    ========================
# --with aix         %_with_aix         1    build aix-federation RPM
# --with authd       %_with_authd       1    build auth-authd RPM
# --with auth_none   %_with_auth_none   1    build auth-none RPM
# --with elan        %_with_elan        1    build switch_elan RPM
# --without munge    %_without_munge    1    don't build auth-munge RPM
# --with bluegene    %_with_bluegene    1    build bluegene RPM
# --with debug       %_with_debug       1    enable extra debugging within SLURM
# --without pam      %_without_pam      1    don't require pam-devel RPM to be installed
# --without readline %_without_readline 1    don't require readline-devel RPM to be installed
# --with sgijob      %_with_sgijob      1    build proctrack-sgi-job RPM

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
%slurm_without_opt elan
%slurm_without_opt authd
%slurm_without_opt bluegene
%slurm_without_opt auth_none
%slurm_without_opt debug

# Build with munge by default on all platforms (disable with --without munge)
%slurm_with_opt munge

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

# Build with sgijob on CHAOS systems
#  (add elan too when it is available)
%if %{?chaos}0
%slurm_with_opt sgijob
%else
%slurm_without_opt sgijob
%endif

Name:    See META file
Version: See META file
Release: See META file

Summary: Simple Linux Utility for Resource Management

License: GPL 
Group: System Environment/Base
Source: %{name}-%{version}-%{release}.tgz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
URL: https://computing.llnl.gov/linux/slurm/
BuildRequires: openssl-devel >= 0.9.6 openssl >= 0.9.6
%ifos linux
BuildRequires: python 
%endif
%if %{slurm_with pam}
BuildRequires: pam-devel
%endif
%if %{slurm_with readline}
BuildRequires: readline-devel
%endif

%description 
SLURM is an open source, fault-tolerant, and highly
scalable cluster management and job scheduling system for Linux clusters
containing up to thousands of nodes. Components include machine status,
partition management, job management, and scheduling modules.

#  Allow override of sysconfdir via _slurm_sysconfdir.
#  Note 'global' instead of 'define' needed here to work around apparent
#   bug in rpm macro scoping (or something...)
%{!?_slurm_sysconfdir: %global _slurm_sysconfdir /etc/slurm}
%define _sysconfdir %_slurm_sysconfdir

#
# Never allow rpm to strip binaries as this will break
#  parallel debugging capability
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

%define _perldir %{_prefix}%{_perlarch}

%package perlapi
Summary: Perl API to SLURM.
Group: Development/System
Requires: slurm
%description perlapi
Perl API package for SLURM.  This package includes the perl API to provide a
helpful interface to SLURM through Perl.

%package devel
Summary: Development package for SLURM.
Group: Development/System
Requires: slurm
%description devel
Development package for SLURM.  This package includes the header files
and static libraries for the SLURM API.

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
SLURM authentication module for Brent Chun's authd
%endif

%if %{slurm_with munge}
%package auth-munge
Summary: SLURM auth implementation using Chris Dunlap's Munge
Group: System Environment/Base
Requires: slurm munge
BuildRequires: munge-devel munge-libs
%description auth-munge
SLURM authentication module for Chris Dunlap's Munge
%endif

%if %{slurm_with bluegene}
%package bluegene
Summary: SLURM interfaces to IBM Blue Gene system
Group: System Environment/Base
Requires: slurm
%description bluegene
SLURM plugin interfaces to IBM Blue Gene system
%endif

%if %{slurm_with elan}
%package switch-elan
Summary: SLURM switch plugin for Quadrics Elan3 or Elan4.
Group: System Environment/Base
Requires: slurm qsnetlibs
BuildRequires: qsnetlibs
%description switch-elan
SLURM switch plugin for Quadrics Elan3 or Elan4.
%endif

%package torque
Summary: Torque/PBS wrappers for transitition from Torque/PBS to SLURM.
Group: Development/System
Requires: slurm
%description torque
Torque wrapper scripts used for helping migrate from Torque/PBS to SLURM.

%if %{slurm_with aix}
%package aix-federation
Summary: SLURM interfaces to IBM AIX and Federation switch.
Group: System Environment/Base
Requires: slurm
BuildRequires: proctrack >= 3
%description aix-federation
SLURM plugins for IBM AIX and Federation switch.
%endif

%if %{slurm_with sgijob}
%package proctrack-sgi-job
Summary: SLURM process tracking plugin for SGI job containers.
Group: System Environment/Base
Requires: slurm
BuildRequires: job
%description proctrack-sgi-job
SLURM process tracking plugin for SGI job containers.
(See http://oss.sgi.com/projects/pagg).
%endif

#############################################################################

%prep
%setup -n %{name}-%{version}-%{release}

%build
%configure --program-prefix=%{?_program_prefix:%{_program_prefix}} \
	%{?slurm_with_debug:--enable-debug} \
    %{?with_proctrack}	\
    %{?with_ssl}		\
    %{?with_munge}      \
	%{!?slurm_with_readline:--without-readline} \
    %{?with_cflags}

make %{?_smp_mflags} 



%install
rm -rf "$RPM_BUILD_ROOT"
mkdir -p "$RPM_BUILD_ROOT"
DESTDIR="$RPM_BUILD_ROOT" make install
DESTDIR="$RPM_BUILD_ROOT" make install-contrib

%ifos aix5.3
mv ${RPM_BUILD_ROOT}%{_bindir}/srun ${RPM_BUILD_ROOT}%{_sbindir}
mv ${RPM_BUILD_ROOT}%{_bindir}/slaunch ${RPM_BUILD_ROOT}%{_sbindir}
%endif

if [ -d /etc/init.d ]; then
   install -D -m755 etc/init.d.slurm $RPM_BUILD_ROOT/etc/init.d/slurm
fi
install -D -m644 etc/slurm.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/slurm.conf.example
install -D -m755 etc/slurm.epilog.clean ${RPM_BUILD_ROOT}%{_sysconfdir}/slurm.epilog.clean

# Delete unpackaged files:
rm -f $RPM_BUILD_ROOT/%{_libdir}/slurm/*.{a,la}

# Build conditional file list for main package
LIST=./slurm.files
touch $LIST
if [ -d /etc/init.d ]; then
   echo "/etc/init.d/slurm" >> $LIST
fi
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/task_affinity.so &&
   echo %{_libdir}/slurm/task_affinity.so >> $LIST

# Build file lists for optional plugin packages
for plugin in auth_munge auth_authd; do
   LIST=./${plugin}.files
   touch $LIST
   test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/${plugin}.so &&
     echo %{_libdir}/slurm/${plugin}.so > $LIST
done

LIST=./switch_elan.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/switch_elan.so &&
  echo %{_libdir}/slurm/switch_elan.so            >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/proctrack_rms.so &&
  echo %{_libdir}/slurm/proctrack_rms.so          >> $LIST

%if %{slurm_with aix}
install -D -m644 etc/federation.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/federation.conf.example
LIST=./aix_federation.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/switch_federation.so &&
  echo %{_libdir}/slurm/switch_federation.so      >> $LIST
test -f  $RPM_BUILD_ROOT/%{_libdir}/slurm/proctrack_aix.so &&
  echo %{_libdir}/slurm/proctrack_aix.so          >> $LIST
test -f  $RPM_BUILD_ROOT/%{_libdir}/slurm/checkpoint_aix.so &&
  echo %{_libdir}/slurm/checkpoint_aix.so         >> $LIST
echo "%config %{_sysconfdir}/federation.conf.example" >> $LIST
%endif

LIST=./perlapi.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_perldir}/Slurm.pm &&
  echo "%{_perldir}/Slurm.pm"                 >> $LIST
test -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurm/Slurm.so &&
  echo "%{_perldir}/auto/Slurm/Slurm.so"      >> $LIST
test -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurm/Slurm.bs &&
  echo "%{_perldir}/auto/Slurm/Slurm.bs"      >> $LIST
test -f $RPM_BUILD_ROOT/%{_perldir}/auto/Slurm/autosplit.ix &&
  echo "%{_perldir}/auto/Slurm/autosplit.ix"      >> $LIST

LIST=./torque.files
touch $LIST
echo "%{_bindir}/pbsnodes"                    >> $LIST
echo "%{_bindir}/qdel"                        >> $LIST
echo "%{_bindir}/qhold"                       >> $LIST
echo "%{_bindir}/qrls"                        >> $LIST
echo "%{_bindir}/qstat"                       >> $LIST
echo "%{_bindir}/qsub"                        >> $LIST
echo "%{_bindir}/mpiexec"                     >> $LIST


%if %{slurm_with bluegene}
install -D -m644 etc/bluegene.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/bluegene.conf.example
LIST=./bluegene.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/select_bluegene.so &&
  echo "%{_libdir}/slurm/select_bluegene.so"      >> $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/libsched_if64.so &&
  echo "%{_libdir}/slurm/libsched_if64.so"        >> $LIST
echo "%{_mandir}/man5/bluegene.*"                 >> $LIST
echo "%{_sbindir}/slurm_epilog"                   >> $LIST
echo "%{_sbindir}/slurm_prolog"                   >> $LIST
echo "%{_sbindir}/sfree"                          >> $LIST
echo "%config %{_sysconfdir}/bluegene.conf.example" >> $LIST
%endif

%if %{slurm_with sgijob}
LIST=./sgi-job.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/proctrack_sgi_job.so &&
echo "%{_libdir}/slurm/proctrack_sgi_job.so" >> $LIST
%endif

#############################################################################

%clean
rm -rf $RPM_BUILD_ROOT
#############################################################################

%files -f slurm.files
%defattr(-,root,root,0755)
%doc AUTHORS
%doc NEWS
%doc README
%doc RELEASE_NOTES
%doc DISCLAIMER
%doc COPYING
%doc etc/slurm.conf.example
%doc doc/html
%{_bindir}/s*
%{_sbindir}/slurmctld
%{_sbindir}/slurmd
%{_sbindir}/slurmstepd
%ifos aix5.3
%{_sbindir}/srun
%{_sbindir}/slaunch
%endif
%{_libdir}/*.so*
%{_libdir}/slurm/src/*
%{_mandir}/man1/*
%{_mandir}/man5/slurm.*
%{_mandir}/man5/wiki.*
%{_mandir}/man8/*
%dir %{_sysconfdir}
%dir %{_libdir}/slurm
%{_libdir}/slurm/checkpoint_none.so
%{_libdir}/slurm/checkpoint_ompi.so
%{_libdir}/slurm/jobacct_gold.so
%{_libdir}/slurm/jobacct_linux.so
%{_libdir}/slurm/jobacct_none.so
%{_libdir}/slurm/jobcomp_none.so
%{_libdir}/slurm/jobcomp_filetxt.so
%{_libdir}/slurm/jobcomp_script.so
%{_libdir}/slurm/proctrack_pgid.so
%{_libdir}/slurm/proctrack_linuxproc.so
%{_libdir}/slurm/sched_backfill.so
%{_libdir}/slurm/sched_builtin.so
%{_libdir}/slurm/sched_hold.so
%{_libdir}/slurm/sched_gang.so
%{_libdir}/slurm/sched_wiki.so
%{_libdir}/slurm/sched_wiki2.so
%{_libdir}/slurm/select_cons_res.so
%{_libdir}/slurm/select_linear.so
%{_libdir}/slurm/switch_none.so
%{_libdir}/slurm/mpi_none.so
%{_libdir}/slurm/mpi_mpich1_p4.so
%{_libdir}/slurm/mpi_mpich1_shmem.so
%{_libdir}/slurm/mpi_mpichgm.so
%{_libdir}/slurm/mpi_mpichmx.so
%{_libdir}/slurm/mpi_mvapich.so
%{_libdir}/slurm/mpi_lam.so
%{_libdir}/slurm/task_none.so
%dir %{_libdir}/slurm/src
%config %{_sysconfdir}/slurm.conf.example
%config %{_sysconfdir}/slurm.epilog.clean
#############################################################################

%files devel
%defattr(-,root,root)
%dir %attr(0755,root,root) %{_prefix}/include/slurm
%{_prefix}/include/slurm/*
%{_libdir}/libpmi.a
%{_libdir}/libpmi.la
%{_libdir}/libslurm.a
%{_libdir}/libslurm.la
%{_mandir}/man3/*
#############################################################################

%if %{slurm_with auth_none}
%files auth-none
%defattr(-,root,root)
%{_libdir}/slurm/auth_none.so
%endif
#############################################################################

%if %{slurm_with munge}
%files -f auth_munge.files auth-munge
%defattr(-,root,root)
%endif
#############################################################################

%if %{slurm_with authd}
%files -f auth_authd.files auth-authd
%defattr(-,root,root)
%endif
#############################################################################

%if %{slurm_with bluegene}
%files -f bluegene.files bluegene
%defattr(-,root,root)
%endif
#############################################################################

%files -f perlapi.files perlapi
%defattr(-,root,root)
#############################################################################

%if %{slurm_with elan}
%files -f switch_elan.files switch-elan
%defattr(-,root,root)
%endif
#############################################################################

%files -f torque.files torque
%defattr(-,root,root)
#############################################################################

%if %{slurm_with aix}
%files -f aix_federation.files aix-federation
%defattr(-,root,root)
%endif
#############################################################################

%if %{slurm_with sgijob}
%files -f sgi-job.files proctrack-sgi-job
%defattr(-,root,root)
%endif
#############################################################################

%pre
#if [ -x /etc/init.d/slurm ]; then
#    if /etc/init.d/slurm status | grep -q running; then
#        /etc/init.d/slurm stop
#    fi
#fi

%post
if [ -x /sbin/ldconfig ]; then
    /sbin/ldconfig %{_libdir}
    /sbin/ldconfig %{_libdir}
    if [ $1 = 1 ]; then
        [ -x /sbin/chkconfig ] && /sbin/chkconfig --add slurm
    fi
fi
if [ ! -f %{_sysconfdir}/slurm.conf ]; then
    echo "You need to build and install a slurm.conf file"
    echo "Edit %{_sysconfdir}/slurm.conf.example and copy it to slurm.conf or"
    echo "Build a new one using http://www.llnl.gov/linux/slurm/configurator.html"
fi


%preun
if [ "$1" = 0 ]; then
    if [ -x /etc/init.d/slurm ]; then
        [ -x /sbin/chkconfig ] && /sbin/chkconfig --del slurm
        if /etc/init.d/slurm status | grep -q running; then
            /etc/init.d/slurm stop
        fi
    fi
fi

%postun
if [ "$1" = 0 ]; then
    if [ -x /sbin/ldconfig ]; then
        /sbin/ldconfig %{_libdir}
    fi
fi
#############################################################################


%changelog
* Tue Feb 14 2006 Morris Jette <jette1@llnl.gov>
- See the NEWS file for update details
