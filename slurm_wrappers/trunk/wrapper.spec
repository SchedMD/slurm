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
# This is a date conversion routine used to make the wrappers compatible with
# LCRM syntax.
#
%ifos aix5.3
  gmake -C Date2Epoch
%else
  make -C Date2Epoch
%endif

############################################################################

%install
rm -rf "$RPM_BUILD_ROOT"
mkdir -p "$RPM_BUILD_ROOT"
chmod -R 755 "$RPM_BUILD_ROOT"

#
# Install the LCRM perl wrappers and support utilities
#
install -D -m755 Date2Epoch/LCRM_date2epoch $RPM_BUILD_ROOT%{_bindir}/LCRM_date2epoch

#
# LCRM wrappers.
#
install -D -m755 wrappers/mjstat.pl $RPM_BUILD_ROOT%{_bindir}/mjstat
install -D -m755 wrappers/palter.pl $RPM_BUILD_ROOT%{_bindir}/palter
install -D -m755 wrappers/phold.pl $RPM_BUILD_ROOT%{_bindir}/phold
install -D -m755 wrappers/prel.pl $RPM_BUILD_ROOT%{_bindir}/prel
install -D -m755 wrappers/prm.pl $RPM_BUILD_ROOT%{_bindir}/prm
install -D -m755 wrappers/pstat.pl $RPM_BUILD_ROOT%{_bindir}/pstat
install -D -m755 wrappers/psub.pl $RPM_BUILD_ROOT%{_bindir}/psub

#
# Moab wrappers.
#
install -D -m755 wrappers/canceljob.pl $RPM_BUILD_ROOT%{_bindir}/canceljob
install -D -m755 wrappers/checkjob.pl $RPM_BUILD_ROOT%{_bindir}/checkjob
install -D -m755 wrappers/checknode.pl $RPM_BUILD_ROOT%{_bindir}/checknode
install -D -m755 wrappers/mdiag.pl $RPM_BUILD_ROOT%{_bindir}/mdiag
install -D -m755 wrappers/mjobctl.pl $RPM_BUILD_ROOT%{_bindir}/mjobctl
install -D -m755 wrappers/moab2slurm.pl $RPM_BUILD_ROOT%{_bindir}/moab2slurm
install -D -m755 wrappers/mshow.pl $RPM_BUILD_ROOT%{_bindir}/mshow
install -D -m755 wrappers/msub.pl $RPM_BUILD_ROOT%{_bindir}/msub
install -D -m755 wrappers/releasehold.pl $RPM_BUILD_ROOT%{_bindir}/releasehold
install -D -m755 wrappers/sethold.pl $RPM_BUILD_ROOT%{_bindir}/sethold
install -D -m755 wrappers/showbf.pl $RPM_BUILD_ROOT%{_bindir}/showbf
install -D -m755 wrappers/showq.pl $RPM_BUILD_ROOT%{_bindir}/showq
install -D -m755 wrappers/showres.pl $RPM_BUILD_ROOT%{_bindir}/showres
install -D -m755 wrappers/showstart.pl $RPM_BUILD_ROOT%{_bindir}/showstart
install -D -m755 wrappers/showstate.pl $RPM_BUILD_ROOT%{_bindir}/showstate


#
# Generate man pages from the perl scripts' perldoc documentation
#
mkdir -p $RPM_BUILD_ROOT%{_mandir}/man1
chmod -R 755 $RPM_BUILD_ROOT%{_mandir}/man1

#
# Do LCRM man pages.
#
for cmd in mjstat palter phold prel prm pstat psub; do
    $RPM_BUILD_ROOT%{_bindir}/${cmd} --roff > $RPM_BUILD_ROOT%{_mandir}/man1/${cmd}.1
done

#
# Do Moab man pages.
#
# No man pages for:
#
#	releasehold sethold
#
for cmd in canceljob checkjob checknode mdiag mjobctl msub showbf showq showres showstart showstate; do
    $RPM_BUILD_ROOT%{_bindir}/${cmd} --roff > $RPM_BUILD_ROOT%{_mandir}/man1/${cmd}.1
done
chmod 644 $RPM_BUILD_ROOT%{_mandir}/man1/*.1

#############################################################################

%clean
rm -rf $RPM_BUILD_ROOT
#############################################################################

%files 
%defattr(-,root,root,0755)
%{_bindir}/*
%{_mandir}/man1/*


%pre

%post

%preun

%postun
#############################################################################

