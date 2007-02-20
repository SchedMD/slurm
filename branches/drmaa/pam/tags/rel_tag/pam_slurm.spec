##
# $Id$
##

Name:		pam_slurm
Version:	0.1.0
Release:	0.pre1

Summary:	PAM module for restricting access to compute nodes via SLURM.
Group:		System Environment/Base
License:	GPL
URL:		http://www.llnl.gov/linux/slurm/

BuildRoot:	%{_tmppath}/%{name}-%{version}
BuildRequires:	/usr/lib/libslurm.so

Source0:	%{name}-%{version}.tgz

%description
This module restricts access to compute nodes in a cluster where Simple 
Linux Utility for Resource Managment (SLURM) is in use.  Access is granted
to root, any user with an SLURM-launched job currently running on the node,
or any user who has allocated resources on the node according to the SLURM
database.

%prep
%setup

%build
make CFLAGS="$RPM_OPT_FLAGS"

%install
rm -rf "$RPM_BUILD_ROOT"
mkdir -p "$RPM_BUILD_ROOT"
make install DESTDIR="$RPM_BUILD_ROOT"

%clean
rm -rf "$RPM_BUILD_ROOT"

%files
%defattr(-,root,root,0755)
%doc COPYING
%doc DISCLAIMER
%doc README
/lib/security/pam_slurm.so
