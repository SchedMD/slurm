# $Id$

# Note that this package is not relocatable

Name:    See META file
Version: See META file
Release: See META file

Summary: Simple Linux Utility for Resource Management

License: GPL 
Group: System Environment/Base
Source: %{name}-%{version}-%{release}.tgz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}
URL: http://www.llnl.gov/linux/slurm
Requires: openssl >= 0.9.6

#
# If "--with debug" is set compile with --enable-debug 
#  and do not strip binaries
#
# (See /usr/share/doc/rpm-*/conditionalbuilds)
#
%if %{?_with_debug:1}%{!?_with_debug:0}
  %define _enable_debug --enable-debug
%endif
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

%{!?_slurm_sysconfdir: %define _slurm_sysconfdir /etc/slurm}
%define _sysconfdir %_slurm_sysconfdir

%package devel
Summary: Development package for SLURM.
Group: Development/System
Requires: slurm

%package auth-none
Summary: SLURM auth NULL implementation (no authentication)
Group: System Environment/Base
Requires: slurm

%package auth-authd
Summary: SLURM auth implementation using Brent Chun's authd
Group: System Environment/Base
Requires: slurm authd

%package auth-munge
Summary: SLURM auth implementation using Chris Dunlap's Munge
Group: System Environment/Base
Requires: slurm munge

%package bluegene
Summary: SLURM interfaces to IBM Blue Gene system
Group: System Environment/Base
Requires: slurm

%package sched-wiki
Summary: SLURM scheduling plugin for the Maui scheduler.
Group: System Environment/Base
Requires: slurm

%package switch-elan
Summary: SLURM switch plugin for Quadrics Elan3 or Elan4.
Group: System Environment/Base
Requires: slurm qsnetlibs

%package aix-federation
Summary: SLURM interfaces to IBM AIX and Federation switch.
Group: System Environment/Base
Requires: slurm

%description 
SLURM is an open source, fault-tolerant, and highly
scalable cluster management and job scheduling system for Linux clusters
containing up to thousands of nodes. Components include machine status,
partition management, job management, and scheduling modules.

%description devel
Development package for SLURM.  This package includes the header files
and static libraries for the SLURM API.

%description auth-none
SLURM NULL authentication module

%description auth-authd
SLURM authentication module for Brent Chun's authd

%description auth-munge
SLURM authentication module for Chris Dunlap's Munge

%description bluegene
SLURM plugin interfaces to IBM Blue Gene system

%description sched-wiki
SLURM scheduling plugin for the Maui scheduler.

%description switch-elan
SLURM switch plugin for Quadrics Elan3 or Elan4.

%description aix-federation
SLURM plugins for IBM AIX and Federation switch.

%prep
%setup -n %{name}-%{version}-%{release}

%build
%configure --program-prefix=%{?_program_prefix:%{_program_prefix}} \
    --sysconfdir=%{_sysconfdir}		\
    %{?_enable_debug}			\
    %{?with_proctrack}			\
    %{?with_ssl}			\
    %{?with_munge}                      \
    %{?with_cflags}

#
# The following was stolen from the E17 packages:
# Build with make -j if SMP is defined in the current environment.
#
if [ "x$SMP" != "x" ]; then
  (make "MAKE=make -k -j $SMP"; exit 0)
  make
else
  make
fi
#############################################################################

%install
rm -rf "$RPM_BUILD_ROOT"
mkdir -p "$RPM_BUILD_ROOT"
DESTDIR="$RPM_BUILD_ROOT" make install

if [ -d /etc/init.d ]; then
   install -D -m755 etc/init.d.slurm $RPM_BUILD_ROOT/etc/init.d/slurm
fi
install -D -m644 etc/slurm.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/slurm.conf.example
install -D -m644 etc/bluegene.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/bluegene.conf.example
install -D -m644 etc/federation.conf.example ${RPM_BUILD_ROOT}%{_sysconfdir}/federation.conf.example
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
for plugin in auth_munge auth_authd sched_wiki; do
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

LIST=./aix_federation.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/switch_federation.so &&
  echo %{_libdir}/slurm/switch_federation.so      >> $LIST
test -f  $RPM_BUILD_ROOT/%{_libdir}/slurm/proctrack_aix.so &&
  echo %{_libdir}/slurm/proctrack_aix.so          >> $LIST
test -f  $RPM_BUILD_ROOT/%{_libdir}/slurm/checkpoint_aix.so &&
  echo %{_libdir}/slurm/checkpoint_aix.so         >> $LIST
echo "%config %{_sysconfdir}/federation.conf.example" >> $LIST

LIST=./bluegene.files
touch $LIST
test -f $RPM_BUILD_ROOT/%{_libdir}/slurm/select_bluegene.so &&
  echo %{_libdir}/slurm/select_bluegene.so        >> $LIST
echo "%{_sbindir}/slurm_epilog"                   >> $LIST
echo "%{_sbindir}/slurm_prolog"                   >> $LIST
echo "%{_sbindir}/sfree"                          >> $LIST
echo "%config %{_sysconfdir}/bluegene.conf.example" >> $LIST

#############################################################################

%clean
rm -rf $RPM_BUILD_ROOT
#############################################################################

%files -f slurm.files
%defattr(-,root,root,0755)
%doc AUTHORS
%doc NEWS
%doc README
%doc DISCLAIMER
%doc COPYING
%doc etc/slurm.conf.example
%doc doc/html
%{_bindir}/*
%{_sbindir}/slurmctld
%{_sbindir}/slurmd
%{_sbindir}/slurmstepd
%{_libdir}/*.so*
%{_libdir}/slurm/src/*
%{_mandir}/man1/*
%{_mandir}/man5/*
%{_mandir}/man8/*
%dir %{_sysconfdir}
%dir %{_libdir}/slurm
%{_libdir}/slurm/checkpoint_none.so
%{_libdir}/slurm/jobacct_log.so
%{_libdir}/slurm/jobacct_none.so
%{_libdir}/slurm/jobcomp_none.so
%{_libdir}/slurm/jobcomp_filetxt.so
%{_libdir}/slurm/jobcomp_script.so
%{_libdir}/slurm/proctrack_pgid.so
%{_libdir}/slurm/proctrack_linuxproc.so
%{_libdir}/slurm/sched_backfill.so
%{_libdir}/slurm/sched_builtin.so
%{_libdir}/slurm/sched_hold.so
%{_libdir}/slurm/select_cons_res.so
%{_libdir}/slurm/select_linear.so
%{_libdir}/slurm/switch_none.so
%{_libdir}/slurm/mpi_none.so
%{_libdir}/slurm/mpi_mpichgm.so
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

%files auth-none
%defattr(-,root,root)
%{_libdir}/slurm/auth_none.so
#############################################################################

%files -f auth_munge.files auth-munge
%defattr(-,root,root)
#############################################################################

%files -f auth_authd.files auth-authd
%defattr(-,root,root)
#############################################################################

%files -f bluegene.files bluegene
%defattr(-,root,root)
#############################################################################

%files -f sched_wiki.files sched-wiki
%defattr(-,root,root)
#############################################################################

%files -f switch_elan.files switch-elan
%defattr(-,root,root)
#############################################################################

%files -f aix_federation.files aix-federation
%defattr(-,root,root)
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
