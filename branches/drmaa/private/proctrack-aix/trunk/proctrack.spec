Summary: Process tracking kernel extension for AIX.
Name:    See META file
Version: See META file
Release: See META file
License: GPL
Group: System Environment/Base
#URL: 
Packager: Christopher J. Morrone <morrone2@llnl.gov>
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root

%description

%prep
%setup -q

%build
gmake PROCTRACK_VERSION=%{version}

%install
rm -rf $RPM_BUILD_ROOT

mkdir -p $RPM_BUILD_ROOT%{_libdir}
install -m644 proctrackext ${RPM_BUILD_ROOT}%{_libdir}
install -m644 proctrackext.exp ${RPM_BUILD_ROOT}%{_libdir}

mkdir -p $RPM_BUILD_ROOT%{_sbindir}
install -m755 proctrack ${RPM_BUILD_ROOT}%{_sbindir}
install -m755 proctrack_version ${RPM_BUILD_ROOT}%{_sbindir}
install -m755 proctrack_list ${RPM_BUILD_ROOT}%{_sbindir}

mkdir -p $RPM_BUILD_ROOT%{_includedir}
install -m644 proctrack.h $RPM_BUILD_ROOT%{_includedir}

%clean
rm -rf $RPM_BUILD_ROOT

%files
%defattr(-,root,root,-)
%{_libdir}/proctrackext
%{_libdir}/proctrackext.exp
%{_sbindir}/proctrack
%{_sbindir}/proctrack_version
%{_sbindir}/proctrack_list
%{_includedir}/proctrack.h

%changelog
* Mon Oct  9 2006 Christopher J. Morrone <morrone2@llnl.gov> - 
- Initial build.

