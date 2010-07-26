# $Id: $id$  $

# Note that this package is not relocatable

Name:    LCRM Slurm Wrappers
Version:
Release:

Summary: LCRM Slurm Wrappers and man pages.

License: llnl
Group: System Environment/Base
Source: %{name}-%{version}.%{release}.tgz
BuildRoot: %{_tmppath}/%{name}-%{version}.%{release}
URL: http://www.llnl.gov
Requires: openssl >= 0.9.6

#
# Never allow rpm to strip binaries as this will break
# parallel debugging capability
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


%description 
A bunch of wrappers to emulate LCRM behavior within Slurm.

%prep
%setup -n %{name}-%{version}.%{release}

%build
%ifarch x86_64 ia64
FPICFLAG="--with-fpic"
%else
FPICFLAG=""
%endif

CC=/usr/bin/gcc
export CC

%ifos aix5.3
  OBJECT_MODE=64
  export OBJECT_MODE
  CC=/usr/bin/gcc
  export CC
%endif

#
# build util.
#
%ifos aix5.3
# This is a date conversion routine used to make the wrappers compatible with
# LCRM syntax.
  gmake -C Date2Epoch
%else
# This is a date conversion routine used to make the wrappers compatible with
# LCRM syntax.
  make -C Date2Epoch
%endif

############################################################################

%install
rm -rf "$RPM_BUILD_ROOT"
mkdir -p "$RPM_BUILD_ROOT"
chmod -R 755 "$RPM_BUILD_ROOT"

# Install the LCRM perl wrappers and support utilities
install -D -m755 Date2Epoch/LCRM_date2epoch $RPM_BUILD_ROOT%{_bindir}/LCRM_date2epoch

#
# Move scripts that aren't supported anymore to bin so if a user tries to use them at least
#
install -D -m755 Wrappers/palter.pl $RPM_BUILD_ROOT%{_bindir}/palter
install -D -m755 Wrappers/phold.pl $RPM_BUILD_ROOT%{_bindir}/phold
install -D -m755 Wrappers/prel.pl $RPM_BUILD_ROOT%{_bindir}/prel
install -D -m755 Wrappers/prm.pl $RPM_BUILD_ROOT%{_bindir}/prm
install -D -m755 Wrappers/pstat.pl $RPM_BUILD_ROOT%{_bindir}/pstat
install -D -m755 Wrappers/psub.pl $RPM_BUILD_ROOT%{_bindir}/psub

#
# Generate man pages from the perl scripts' perldoc documentation
#
mkdir -p $RPM_BUILD_ROOT%{_mandir}/man1
chmod -R 755 $RPM_BUILD_ROOT%{_mandir}/man1
for cmd in palter phold prel prm pstat psub; do
    $RPM_BUILD_ROOT%{_bindir}/${cmd} --roff > $RPM_BUILD_ROOT%{_mandir}/man1/${cmd}.1
done
chmod 644 $RPM_BUILD_ROOT%{_mandir}/man1/*.1

#############################################################################

%clean
rm -rf $RPM_BUILD_ROOT
#############################################################################

%files 

%{_bindir}/p*
%{_bindir}/L*
%{_mandir}/man1/*

%defattr(-,root,root,0755)

%pre

%post

%preun

%postun
#############################################################################

