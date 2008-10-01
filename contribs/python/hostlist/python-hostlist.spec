%{!?python_sitelib: %define python_sitelib %(%{__python} -c "from distutils.sysconfig import get_python_lib; print get_python_lib()")}

Name:           python-hostlist
Version:        1.3
Release:        1
Summary:        Python module for hostlist handling
Vendor:         NSC

Group:          Development/Languages
License:        GPL2+
URL:            http://www.nsc.liu.se/~kent/python-hostlist/
Source0:        http://www.nsc.liu.se/~kent/python-hostlist/%{name}-%{version}.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildArch:      noarch
BuildRequires:  python-devel

%description
The hostlist.py module knows how to expand and collect hostlist
expressions. The package also includes the 'hostlist' binary which can
be used to collect/expand hostlists and perform set operations on
them.

%prep
%setup -q


%build
%{__python} setup.py build


%install
rm -rf $RPM_BUILD_ROOT
%{__python} setup.py install -O1 --skip-build --prefix /usr --root $RPM_BUILD_ROOT

 
%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%doc README
%doc COPYING
%doc CHANGES
%{python_sitelib}/*
/usr/bin/hostlist
/usr/share/man/man1/hostlist.1.gz
%changelog
