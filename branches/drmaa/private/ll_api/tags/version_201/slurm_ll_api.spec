Summary: Library allowing IBM's poe command to interface with SLURM.
Name:    See META file
Version: See META file
Release: See META file
License: Proprietary
Group: System Environment/Base
#URL: 
Packager: Christopher J. Morrone <morrone2@llnl.gov>
Source0: %{name}-%{version}.tar.gz
BuildRoot: %{_tmppath}/%{name}-%{version}-%{release}-root
Requires: slurm >= 1.2.0
BuildRequires: slurm-devel >= 1.2.0

%description
slurm_ll_api allows IBM's "poe" command to allocate node resources
and launch job steps using SLURM rather than LoadLeveler.  If
the MP_RMLIB environment variable contains the fully qualified path
to slurm_ll_api.so, poe will dlopen the slurm_ll_api rather than
the normal LoadLeveler library.  The slurm_ll_api then emulates
selected LoadLeveler API functions and translates them into
the closest equivalent SLURM API calls.

%prep
%setup -q

%build
gmake -j4 PROJECT=%{name} SLURM_LL_API_VERSION=%{version} SLURM=%{_prefix}

%install
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT%{_libdir}
install -m644 slurm_ll_api.so ${RPM_BUILD_ROOT}%{_libdir}

%clean
rm -rf $RPM_BUILD_ROOT


%files
%defattr(-,root,root,-)
%{_libdir}/slurm_ll_api.so

%changelog
* Fri Oct  6 2006 Christopher J. Morrone <morrone2@llnl.gov> - 
- Initial build.

