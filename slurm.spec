Name: slurm
Version: .9
Release: 1
Summary: Simple Linux Utility for Resource Management
Copyright: Copyright (C) 2002 The Regents of the University of California.  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).  UCRL-CODE-2002-040.
License: GPL
Group: System Environment/Base
Source: %{name}-%{version}-%{release}.tgz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}

%define _with_authd 0
%define _with_elan 0
%define _with_totalview 0

%package 
Summary: Common files and client utils for SLURM
Group: System Environment/Base
Requires: authd libreadline libpopt
Prereq: 

%package devel
Summary: Devel package for SLURM.
Group: System Environment/Base
Requires: slurm

%package daemon
Summary: The compute node daemon of SLURM.
Group: System Environment/Base
Requires: slurm

%package controller
Summary: The central management daemon of SLURM.
Group: System Environment/Base
Requires: slurm 


%description 
The common slurm package, which includes the base libraries and the client utilitues for SLURM.

%description devel
Development package for SLURM.  This package includes the header files and static libraries.

%description daemon
Slurmd  is the compute node daemon of Slurm. It monitors all tasks running on the compute node , accepts work (tasks), launches tasks, and kills running tasks upon request.

%description slurmctld
Slurmctld  is  the central management daemon of Slurm. It monitors all other Slurm daemons and resources, accepts work (jobs), and allocates resources to those jobs.


%prep
%setup -n %{name}-%{version}-%{release}

%build
%if %{_with_elan}
	WITH_ELAN=--with-elan
%endif
%if %{_with_authd}
	WITH_AUTHD=--with-authd
%endif
%if %{_with_totalview}
	WITH_TOTALVIEW=--with-totalview
%endif


./configure --prefix=%{_prefix} --with-slurm-conf=/etc/slurm $WITHELAN $WITH_ELAN $WITH_AUTHD $WITH_TOTALVIEW
make

%install

%makeinstall

%clean
rm -rf $RPM_BUILD_ROOT

%files
# where does slurm.conf belong ??? Or should be throw it in the doc directory?
%defattr(-,root,root)
%doc README ChangeLog 
%attr(4755, root, root) %{_bindir}/cancel
%attr(4755, root, root) %{_bindir}/scontrol
%attr(4755, root, root) %{_bindir}/sinfo
%attr(4755, root, root) %{_bindir}/squeue
%attr(4755, root, root) %{_bindir}/srun 
%{_libdir}/*.so
%{_mandir}/man1/*
%{_mandir}/man5/slurm.conf.5

%files devel
%defattr(-,root,root)
%{_prefix}/include/slurm.h
%{_libdir}/libslurm.a
%{_libdir}/libslurm.la
%{_mandir}/man3/*

%files daemon
%defattr(-,root,root)
%attr(4755, root, root) %{_sbindir}/slurmd
%attr(4755, root, root) /etc/init.d/slurmd
%{_mandir}/man8/slurmd.8

%files controller
%defattr(-,root,root)
%attr(4755, root, root) %{_sbindir}/slurmcltd
%attr(4755, root, root) /etc/init.d/slurmctld
%{_mandir}/man8/slurmctld.8


%post

%changelog
* Sat Jan 26 2003 Joey Ekstrom <jcekstrom@llnl.gov> 
- Started spec file
-
